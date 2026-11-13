/* Round-trip regression tests for assigning SDL inputs to Saturn controls. */
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include "persdljoy.h"

PerInterface_struct *PERCoreList[] = { &PERSDLJoy, NULL };

/* Input polling normally executes a frame; these tests inspect the pad bytes. */
int YabauseExec(void) { return 0; }

static int failures;
static SDL_Joystick *joystick;
static const u32 digital_flags = PERSF_KEY | PERSF_BUTTON | PERSF_HAT | PERSF_AXIS;

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void set_axis(int axis, Sint16 value)
{
    if (SDL_JoystickSetVirtualAxis(joystick, axis, value) != 0)
    {
        fprintf(stderr, "SDL axis: %s\n", SDL_GetError());
        exit(2);
    }
}

static void set_hat(Uint8 value)
{
    if (SDL_JoystickSetVirtualHat(joystick, 0, value) != 0)
    {
        fprintf(stderr, "SDL hat: %s\n", SDL_GetError());
        exit(2);
    }
}

static PerPad_struct *new_pad(void)
{
    int key;
    PerPad_struct *pad;
    PerPortReset();
    pad = PerPadAdd(&PORTDATA1);
    for (key = PERPAD_UP; key <= PERPAD_Z; key++)
        PerSetKey(0xffffffffu, (u8)key, pad);
    return pad;
}

static void expect_directions(PerPad_struct *pad, unsigned int pressed, const char *message)
{
    PERSDLJoy.HandleEvents();
    check((pad->padbits[0] & 0xf0) == (0xf0 ^ pressed), message);
}

static void test_axis_directions(void)
{
    const int axes[] = { 1, 0, 1, 0 };
    const Sint16 values[] = { -32768, 32767, 32767, -32768 };
    const unsigned int bits[] = { 0x10, 0x80, 0x20, 0x40 };
    u32 keys[4];
    PerPad_struct *pad = new_pad();
    int direction;

    for (direction = 0; direction < 4; direction++)
    {
        set_axis(axes[direction], values[direction]);
        keys[direction] = PERSDLJoy.Scan(digital_flags);
        check(keys[direction] != 0, "axis direction is detected while assigning");
        PerSetKey(keys[direction], (u8)direction, pad);
        set_axis(axes[direction], 0);
        PERSDLJoy.HandleEvents();
    }
    check(keys[PERPAD_UP] != keys[PERPAD_DOWN], "up and down have distinct bindings");
    check(keys[PERPAD_LEFT] != keys[PERPAD_RIGHT], "left and right have distinct bindings");
    for (direction = 0; direction < 4; direction++)
    {
        set_axis(axes[direction], values[direction]);
        expect_directions(pad, bits[direction], "assigned axis presses its Saturn direction");
        expect_directions(pad, bits[direction], "axis direction remains held on the next frame");
        set_axis(axes[direction], 0);
        expect_directions(pad, 0, "centering the axis releases its Saturn direction");
    }
    set_axis(0, -32768);
    set_axis(1, -32768);
    expect_directions(pad, 0x50, "axes support diagonals");
    set_axis(0, 32767);
    set_axis(1, 32767);
    expect_directions(pad, 0xa0, "reversing axes releases the previous directions");
    set_axis(0, 1000);
    set_axis(1, -1000);
    expect_directions(pad, 0, "axis dead zone releases digital directions");
    set_axis(0, 0);
    set_axis(1, 0);
    puts("Checked axis direction assignment and polling");
}

static void test_hats_and_buttons(void)
{
    const Uint8 hats[] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
    const unsigned int bits[] = { 0x10, 0x80, 0x20, 0x40 };
    PerPad_struct *pad = new_pad();
    int direction;
    u32 key;

    for (direction = 0; direction < 4; direction++)
    {
        set_hat(hats[direction]);
        key = PERSDLJoy.Scan(digital_flags);
        check(key != 0, "hat direction is detected while assigning");
        PerSetKey(key, (u8)direction, pad);
        set_hat(SDL_HAT_CENTERED);
        PERSDLJoy.HandleEvents();
    }
    for (direction = 0; direction < 4; direction++)
    {
        set_hat(hats[direction]);
        expect_directions(pad, bits[direction], "assigned hat presses its Saturn direction");
        expect_directions(pad, bits[direction], "hat direction remains held");
        set_hat(SDL_HAT_CENTERED);
        expect_directions(pad, 0, "centering the hat releases its Saturn direction");
    }
    set_hat(SDL_HAT_RIGHTUP);
    expect_directions(pad, 0x90, "hat supports diagonals");
    set_hat(SDL_HAT_LEFTDOWN);
    expect_directions(pad, 0x60, "hat diagonal changes release previous directions");
    set_hat(SDL_HAT_CENTERED);
    expect_directions(pad, 0, "hat diagonal releases");

    check(SDL_JoystickSetVirtualButton(joystick, 0, 1) == 0, "set virtual button");
    key = PERSDLJoy.Scan(digital_flags);
    check(key != 0, "button is detected while assigning");
    PerSetKey(key, PERPAD_A, pad);
    PERSDLJoy.HandleEvents();
    check((pad->padbits[0] & 4) == 0, "assigned button presses Saturn A");
    check(SDL_JoystickSetVirtualButton(joystick, 0, 0) == 0, "release virtual button");
    PERSDLJoy.HandleEvents();
    check((pad->padbits[0] & 4) != 0, "assigned button releases Saturn A");
    puts("Checked hat and button assignment and polling");
}

static void test_analog_axis(void)
{
    int control;
    u32 key;
    PerAnalog_struct *pad;
    PerPortReset();
    pad = Per3DPadAdd(&PORTDATA1);
    for (control = PERPAD_UP; control <= PERPAD_Z; control++)
        PerSetKey(0xffffffffu, (u8)control, pad);
    for (control = PERANALOG_AXIS1; control <= PERANALOG_AXIS7; control++)
        PerSetKey(0xffffffffu, (u8)control, pad);
    set_axis(0, -32768);
    key = PERSDLJoy.Scan(PERSF_AXIS);
    check(key != 0, "analog-only assignment detects the axis");
    PerSetKey(key, PERANALOG_AXIS1, pad);
    PERSDLJoy.HandleEvents();
    check(pad->analogbits[2] == 0, "analog minimum is preserved");
    set_axis(0, 32767);
    check(PERSDLJoy.Scan(PERSF_AXIS) == key, "both analog directions use the same axis binding");
    PERSDLJoy.HandleEvents();
    check(pad->analogbits[2] == 255, "analog maximum is preserved");
    set_axis(0, 0);
    PERSDLJoy.HandleEvents();
    check(pad->analogbits[2] == 128, "analog center is preserved");
    puts("Checked 3D pad analog assignment and polling");
}

int main(void)
{
    int device;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK) != 0)
        return 2;
    device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 1, 1);
    if (device < 0 || !(joystick = SDL_JoystickOpen(device)) || PERSDLJoy.Init() != 0)
    {
        fprintf(stderr, "SDL initialization: %s\n", SDL_GetError());
        return 2;
    }
    test_axis_directions();
    test_hats_and_buttons();
    test_analog_axis();
    PerPortReset();
    PERSDLJoy.DeInit();
    SDL_JoystickClose(joystick);
    SDL_JoystickDetachVirtual(device);
    SDL_Quit();
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
