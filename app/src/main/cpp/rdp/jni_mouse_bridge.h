#pragma once

class RdpInputForwarder;

// Set the forwarder that the JNI mouse bridge will use.
void JniMouseBridge_SetForwarder(RdpInputForwarder* fwd);
