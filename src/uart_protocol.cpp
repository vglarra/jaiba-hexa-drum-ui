#include "uart_protocol.h"

#include "drum_state.h"

namespace {

constexpr int UART_LINE_MAX = 63;
constexpr unsigned long ACK_TIMEOUT_MS = 1000; // every round-trip so far has been well under 100ms

struct PendingAck {
    bool active = false;
    String command;
    unsigned long sentAt = 0;
    int padIndex = -1; // only meaningful when command is a pad-field setter (see isPadFieldSetter)
};

PendingAck pendingAck;
CommandResult lastResult = CommandResult::NONE;
String lastFailureReason;

String commandName(const String& line) {
    int comma = line.indexOf(',');
    return comma < 0 ? line : line.substring(0, comma);
}

bool expectsAck(const String& name) {
    return name.startsWith("SET_") || name == "CAL_START";
}

// These three all take padIndex as their first argument and represent a
// single pad's live-readable fields (see PADVAL) -- on success, drumState
// should be resynced for that pad without the screen having to ask.
bool isPadFieldSetter(const String& name) {
    return name == "SET_NOTE" || name == "SET_THRESH" || name == "SET_CEILING_BASELINE";
}

// Extracts the field at `index` from a comma-separated argument string.
String fieldAt(const String& args, int index) {
    int start = 0;
    for (int i = 0; i < index; i++) {
        int next = args.indexOf(',', start);
        if (next < 0) return "";
        start = next + 1;
    }
    int end = args.indexOf(',', start);
    return end < 0 ? args.substring(start) : args.substring(start, end);
}

int intArg(const String& args, int index) {
    return fieldAt(args, index).toInt();
}

float floatArg(const String& args, int index) {
    return fieldAt(args, index).toFloat();
}

String stringArg(const String& args, int index) {
    String field = fieldAt(args, index);
    field.trim();
    return field;
}

void handleHit(const String& args) {
    // No persisted state yet -- HIT is an event stream for the future Hit
    // Monitor screen (UI_PLAN.md), which doesn't exist yet. Debug-log only.
    Serial.print("HIT ");
    Serial.println(args);
}

void handlePadval(const String& args) {
    int padIndex = intArg(args, 0);
    if (padIndex < 0 || padIndex >= NUM_PADS) return;

    PadState& pad = drumState.pads[padIndex];
    pad.note = intArg(args, 1);
    pad.threshold = intArg(args, 2);
    pad.ceiling = intArg(args, 3);
    pad.curveExp = floatArg(args, 4);
    pad.lastUpdated = millis();
}

CalSensor parseCalSensor(const String& s) {
    if (s == "PIEZO") return CalSensor::PIEZO;
    if (s == "VELOSTAT") return CalSensor::VELOSTAT;
    return CalSensor::NONE;
}

CalPhase parseCalPhase(const String& s) {
    if (s == "RESTING") return CalPhase::RESTING;
    if (s == "WAITING_TOUCH") return CalPhase::WAITING_TOUCH;
    if (s == "CAPTURING") return CalPhase::CAPTURING;
    if (s == "PAD_DONE") return CalPhase::PAD_DONE;
    if (s == "COMPLETE") return CalPhase::COMPLETE;
    if (s == "ABORTED") return CalPhase::ABORTED;
    return CalPhase::NONE;
}

void handleCalState(const String& args) {
    CalPhase phase = parseCalPhase(stringArg(args, 1));
    bool hasPadIndex = (phase == CalPhase::CAPTURING || phase == CalPhase::PAD_DONE);

    drumState.calState.sensor = parseCalSensor(stringArg(args, 0));
    drumState.calState.phase = phase;
    drumState.calState.padIndex = hasPadIndex ? intArg(args, 2) : -1;
    drumState.calState.lastUpdated = millis();
}

void handleXtalkval(const String& args) {
    drumState.crosstalkRatio = floatArg(args, 0);
    drumState.crosstalkWindowMs = intArg(args, 1);
    drumState.xtalkLastUpdated = millis();
}

PlayMode parsePlayMode(const String& s) {
    if (s == "BOTH") return PlayMode::BOTH;
    if (s == "PIEZO_ONLY") return PlayMode::PIEZO_ONLY;
    if (s == "VELOSTAT_ONLY") return PlayMode::VELOSTAT_ONLY;
    return PlayMode::UNKNOWN;
}

void handlePlayMode(const String& args) {
    drumState.playMode = parsePlayMode(stringArg(args, 0));
    drumState.playModeLastUpdated = millis();
}

void handleAck(const String& args) {
    // ACK,<command> echoes back which command succeeded -- check it against
    // what's actually pending rather than trusting any ACK that arrives
    // while something's pending. Otherwise a late ACK for a since-timed-out
    // command could land after a *different* command has taken the pending
    // slot, and silently mark that unrelated command as successful.
    String ackedCommand = args;
    ackedCommand.trim();

    if (!pendingAck.active || ackedCommand != pendingAck.command) {
        Serial.print("Stray ACK: ");
        Serial.println(args);
        return;
    }

    bool needsPadSync = isPadFieldSetter(pendingAck.command);
    int padIndex = pendingAck.padIndex;

    pendingAck.active = false;
    lastResult = CommandResult::SUCCESS;

    if (needsPadSync && padIndex >= 0) {
        // Plain query -- expectsAck() doesn't match GET_PAD, so this
        // doesn't touch the pending-ACK slot we just cleared above.
        sendCommand("GET_PAD," + String(padIndex));
    }
}

void handleErr(const String& args) {
    // ERR,<reason> does NOT echo the command name, unlike ACK. Correlation
    // relies on the single-pending-slot assumption documented on
    // sendCommand(): if something is pending, this ERR must be for it.
    if (!pendingAck.active) {
        Serial.print("Stray ERR: ");
        Serial.println(args);
        return;
    }
    pendingAck.active = false;
    lastResult = CommandResult::FAILED;
    lastFailureReason = args;
}

void handleIncomingLine(const String& line) {
    if (line.length() == 0) return;

    int comma = line.indexOf(',');
    String cmd = commandName(line);
    String args = comma < 0 ? "" : line.substring(comma + 1);

    if (cmd == "HIT") handleHit(args);
    else if (cmd == "PADVAL") handlePadval(args);
    else if (cmd == "CAL_STATE") handleCalState(args);
    else if (cmd == "XTALKVAL") handleXtalkval(args);
    else if (cmd == "PLAY_MODE") handlePlayMode(args);
    else if (cmd == "ACK") handleAck(args);
    else if (cmd == "ERR") handleErr(args);
    else {
        Serial.print("Unknown message: ");
        Serial.println(line);
    }
}

} // namespace

void uartProtocolInit() {
    Serial1.begin(115200);
}

void uartProtocolUpdate() {
    static String line;

    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n') {
            line.trim(); // drop trailing \r
            handleIncomingLine(line);
            line = "";
        } else {
            line += c;
            if (line.length() > UART_LINE_MAX) line = ""; // discard garbage/noise line
        }
    }
}

void sendCommand(const String& cmd) {
    Serial1.print(cmd);
    Serial1.print('\n');

    String name = commandName(cmd);
    if (expectsAck(name)) {
        int comma = cmd.indexOf(',');
        String args = comma < 0 ? "" : cmd.substring(comma + 1);

        pendingAck.active = true;
        pendingAck.command = name;
        pendingAck.sentAt = millis();
        pendingAck.padIndex = isPadFieldSetter(name) ? intArg(args, 0) : -1;
        lastResult = CommandResult::PENDING;
    }
}

CommandResult getLastCommandResult() {
    if (pendingAck.active && millis() - pendingAck.sentAt > ACK_TIMEOUT_MS) {
        pendingAck.active = false;
        lastResult = CommandResult::TIMED_OUT;
    }
    return lastResult;
}

const String& getLastFailureReason() {
    return lastFailureReason;
}
