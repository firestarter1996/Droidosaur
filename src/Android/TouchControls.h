// TOUCH CONTROLS FOR ANDROID
// Virtual joystick + action buttons for Nanosaur on Android.
#pragma once

#ifdef __ANDROID__

#include <stdbool.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Button IDs for touch controls
typedef enum
{
    kTouchBtn_Jump     = 0,
    kTouchBtn_Attack   = 1,
    kTouchBtn_Pickup   = 2,
    kTouchBtn_Pause    = 3,
    kTouchBtn_COUNT
} TouchButtonID;

// Initialize the touch control system (call after GL context is ready)
void TouchControls_Init(void);

// Shutdown the touch control system
void TouchControls_Shutdown(void);

// Process an SDL event (call from input handling for touch events)
bool TouchControls_ProcessEvent(const SDL_Event *event);

// Query joystick analog values (-1..1)
float TouchControls_GetJoystickX(void);
float TouchControls_GetJoystickY(void);

// Query button state
bool TouchControls_IsButtonDown(TouchButtonID btn);

// Draw the touch controls overlay (call at end of each frame)
void TouchControls_Draw(void);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
