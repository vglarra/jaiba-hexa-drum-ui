#pragma once

#include <Arduino.h>

// Result of the most recent SET_*/CAL_START command sent via sendCommand().
// GET_* queries don't use this -- their responses land directly in
// drumState (see drum_state.h) and screens read the state, not this result.
enum class CommandResult { NONE, PENDING, SUCCESS, FAILED, TIMED_OUT };

// Must be called once from setup() before any other uart_protocol function.
void uartProtocolInit();

// Must be called every loop() iteration -- reads Serial1, accumulates
// lines, and dispatches complete messages into drumState.
void uartProtocolUpdate();

// Sends a command line to the drum Teensy (newline appended automatically).
//
// NOTE: only one SET_*/CAL_START command can be in flight at a time --
// there is a single pending-ACK slot, not a queue. Sending a second
// mutating command before the first resolves (see getLastCommandResult())
// silently overwrites the pending slot, so the first command's eventual
// ACK/ERR will look like a stray/unmatched response. Screens must wait
// for getLastCommandResult() to leave PENDING before firing another
// SET_*/CAL_START.
void sendCommand(const String& cmd);

// Status of the last SET_*/CAL_START command sent. Returns TIMED_OUT if
// no ACK/ERR arrived within the timeout window, so a screen never gets
// stuck showing an indefinite "waiting" spinner on a dropped message.
CommandResult getLastCommandResult();

// Reason string from the most recent ERR. Only meaningful when
// getLastCommandResult() == FAILED.
const String& getLastFailureReason();
