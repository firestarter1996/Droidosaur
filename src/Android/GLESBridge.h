// GLES BRIDGE HEADER
// Declares the init/shutdown entry points for the GLES compatibility bridge.
#pragma once

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

void GLESBridge_Init(void);
void GLESBridge_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
