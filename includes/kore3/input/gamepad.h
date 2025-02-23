#ifndef KORE_INPUT_GAMEPAD_HEADER
#define KORE_INPUT_GAMEPAD_HEADER

#include <kore3/global.h>

/*! \file gamepad.h
    \brief Provides gamepad-support.
*/

#ifdef __cplusplus
extern "C" {
#endif

#define KORE_GAMEPAD_MAX_COUNT 12

/// <summary>
/// Sets the gamepad-connect-callback which is called when a gamepad is connected.
/// </summary>
/// <param name="value">The callback</param>
/// <param name="userdata">Userdata you will receive back as the 2nd callback parameter</param>
KORE_FUNC void kore_gamepad_set_connect_callback(void (*value)(int /*gamepad*/, void * /*userdata*/), void *userdata);

/// <summary>
/// Sets the gamepad-disconnect-callback which is called when a gamepad is disconnected.
/// </summary>
/// <param name="value">The callback</param>
/// <param name="userdata">Userdata you will receive back as the 2nd callback parameter</param>
KORE_FUNC void kore_gamepad_set_disconnect_callback(void (*value)(int /*gamepad*/, void * /*userdata*/), void *userdata);

/// <summary>
/// Sets the gamepad-axis-callback which is called with data about changing gamepad-sticks.
/// </summary>
/// <param name="value">The callback</param>
/// <param name="userdata">Userdata you will receive back as the 4th callback parameter</param>
KORE_FUNC void kore_gamepad_set_axis_callback(void (*value)(int /*gamepad*/, int /*axis*/, float /*value*/, void * /*userdata*/), void *userdata);

/// <summary>
/// Sets the gamepad-button-callback which is called with data about changing gamepad-buttons.
/// </summary>
/// <param name="value">The callback</param>
/// <param name="userdata">Userdata you will receive back as the 4th callback parameter</param>
KORE_FUNC void kore_gamepad_set_button_callback(void (*value)(int /*gamepad*/, int /*button*/, float /*value*/, void * /*userdata*/), void *userdata);

/// <summary>
/// Sets the preferred number of gamepads for the application to work properly. The platform may display a system
/// dialog to ask for more players to join the party. The operation is not guaranteed to succeed.
/// </summary>
/// <param name="count">The desired number of connected gamepads</param>
KORE_FUNC void kore_gamepad_set_count(int count);

/// <summary>
/// Returns a vendor-name for a gamepad.
/// </summary>
/// <param name="gamepad">The index of the gamepad for which to receive the vendor-name</param>
/// <returns>The vendor-name</returns>
KORE_FUNC const char *kore_gamepad_vendor(int gamepad);

/// <summary>
/// Returns a name for a gamepad.
/// </summary>
/// <param name="gamepad">The index of the gamepad for which to receive the name</param>
/// <returns>The gamepad's name</returns>
KORE_FUNC const char *kore_gamepad_product_name(int gamepad);

/// <summary>
/// Checks whether a gamepad is connected.
/// </summary>
/// <param name="gamepad">The index of the gamepad which's connection will be checked</param>
/// <returns>Whether a gamepad is connected for the gamepad-index</returns>
KORE_FUNC bool kore_gamepad_connected(int gamepad);

/// <summary>
/// Rumbles a gamepad. Careful here because it might just fall off your table.
/// </summary>
/// <param name="gamepad">The index of the gamepad to rumble</param>
/// <param name="left">Rumble-strength for the left motor between 0 and 1</param>
/// <param name="right">Rumble-strength for the right motor between 0 and 1</param>
KORE_FUNC void kore_gamepad_rumble(int gamepad, float left, float right);

void kore_internal_gamepad_trigger_connect(int gamepad);
void kore_internal_gamepad_trigger_disconnect(int gamepad);
void kore_internal_gamepad_trigger_axis(int gamepad, int axis, float value);
void kore_internal_gamepad_trigger_button(int gamepad, int button, float value);

#ifdef __cplusplus
}
#endif

#endif
