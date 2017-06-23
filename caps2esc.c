// compile:
//     gcc caps2esc.c -o caps2esc -I/usr/include/libevdev-1.0 -levdev -ludev
// run:
//     sudo nice -n -20 ./caps2esc >caps2esc.log 2>caps2esc.err &

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libudev.h>
#include <sys/select.h>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#define add_key(key)                                                           \
    const struct input_event                                                   \
        key##_UP = {.type = EV_KEY, .code = KEY_##key, .value = 0},            \
        key##_DOWN = {.type = EV_KEY, .code = KEY_##key, .value = 1},          \
        key##_REPEAT = {.type = EV_KEY, .code = KEY_##key, .value = 2};        \
    static int key##_IS_DOWN = 0, key##_GIVE_UP = 0

add_key(SPACE);
add_key(KP1);
add_key(ESC);
add_key(CAPSLOCK);

// clang-format off
/*
 * const struct input_event
 * esc_up          = {.type = EV_KEY, .code = KEY_I,      .value = 0},
 * ctrl_up         = {.type = EV_KEY, .code = KEY_SPACE, .value = 0},
 * capslock_up     = {.type = EV_KEY, .code = KEY_ESC, .value = 0},
 * esc_down        = {.type = EV_KEY, .code = KEY_I,      .value = 1},
 * ctrl_down       = {.type = EV_KEY, .code = KEY_SPACE, .value = 1},
 * capslock_down   = {.type = EV_KEY, .code = KEY_ESC, .value = 1},
 * esc_repeat      = {.type = EV_KEY, .code = KEY_I,      .value = 2},
 * ctrl_repeat     = {.type = EV_KEY, .code = KEY_SPACE, .value = 2},
 * capslock_repeat = {.type = EV_KEY, .code = KEY_ESC, .value = 2};
 */
// clang-format on

int equal(const struct input_event * first, const struct input_event * second) {
    return first->type == second->type && first->code == second->code &&
           first->value == second->value;
}

#define DUAL_KEY(S, N, I, O)                                                   \
    dual_key(I, O, N##_UP, S##_UP, N##_REPEAT, N##_DOWN, S##_DOWN,             \
             &(N##_IS_DOWN), &(S##_GIVE_UP))

#define DUAL_KEY_INV(S, N, I, O)                                               \
    dual_key_inv(I, O, N##_UP, S##_UP, N##_REPEAT, N##_DOWN, S##_DOWN,         \
                 &(N##_IS_DOWN), &(S##_GIVE_UP))

int dual_key(const struct input_event * input, struct input_event output[],
             const struct input_event normal_up,
             const struct input_event special_up,
             const struct input_event normal_repeat,
             const struct input_event normal_down,
             const struct input_event special_down, int * normal_is_down,
             int * special_give_up) {

    if(*normal_is_down) {
        if(equal(input, &normal_down) || equal(input, &normal_repeat) ||
           input->code == special_up.code) {
            return 0;
        }

        if(equal(input, &normal_up)) {
            *normal_is_down = 0;

            if(*special_give_up) {
                *special_give_up = 0;
                output[0]        = normal_up;
                return 1;
            } else {
                output[0] = special_down;
                output[1] = special_up;
                return 2;
            }
        } else {
            int k = 0;

            if(!(*special_give_up) && input->value) {
                *special_give_up = 1;
                output[k++]      = normal_down;
            }

            output[k++] = *input;

            return k;
        }
    }

    if(equal(input, &normal_down)) {
        *normal_is_down = 1;
        return 0;
    }

    output[0] = *input;

    return 1;
}

int dual_key_inv(const struct input_event * input, struct input_event output[],
                 const struct input_event normal_up,
                 const struct input_event special_up,
                 const struct input_event normal_repeat,
                 const struct input_event normal_down,
                 const struct input_event special_down, int * normal_is_down,
                 int * special_give_up) {

    fprintf(stdout, "inputcode: %d,space is %d,value: %d\n", input->code,
            KEY_SPACE, input->value);
    if(*normal_is_down) {
        fprintf(stdout, "space was down\n");
        if(equal(input, &normal_down) || equal(input, &normal_repeat) ||
           input->code == normal_up.code) {
            return 0;
        }

		fprintf(stdout, "looking for down: %d,value: %d\n", normal_up.code,normal_up.value);
        if(equal(input, &normal_up)) {
            fprintf(stdout, "space goes up\n");
            *normal_is_down = 0;

            if(*special_give_up) {
                *special_give_up = 0;
                output[0]        = special_up;
                return 1;
            } else {
                output[0] = normal_down;
                output[1] = normal_up;
                return 2;
            }
        } else {
            int k = 0;

            if(!(*special_give_up) && input->value) {
                *special_give_up = 1;
                output[k++]      = special_down;
            }

            output[k++] = *input;

            return k;
        }
    }

    if(equal(input, &normal_down)) {
        fprintf(stdout, "space goes down\n");
        *normal_is_down = 1;
        return 0;
    }

    output[0] = *input;

    return 1;
}

int eventmap(const struct input_event * input, struct input_event output[]) {
    if(input->type != EV_KEY) {
        output[0] = *input;
        return 1;
    }

    int k = 0;
    k += DUAL_KEY(KP1, SPACE, input, &output[k]);
    k += DUAL_KEY(ESC, CAPSLOCK, input, &output[k]);

    return k;
}

int eventmap_loop(const char * devnode) {
    int result = 0;
    int fd     = open(devnode, O_RDONLY);
    if(fd < 0) return 0;

    struct libevdev * dev;
    if(libevdev_new_from_fd(fd, &dev) < 0) goto teardown_fd;

    sleep(1);

    if(libevdev_grab(dev, LIBEVDEV_GRAB) < 0) goto teardown_dev;
    if(libevdev_enable_event_type(dev, EV_KEY) < 0) goto teardown_grab;
    if(libevdev_enable_event_code(dev, EV_KEY, KEY_ESC, NULL) < 0)
        goto teardown_grab;
    if(libevdev_enable_event_code(dev, EV_KEY, KEY_CAPSLOCK, NULL) < 0)
        goto teardown_grab;
    if(libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTSHIFT, NULL) < 0)
        goto teardown_grab;
    if(libevdev_enable_event_code(dev, EV_KEY, KEY_SPACE, NULL) < 0)
        goto teardown_grab;
    if(libevdev_disable_event_code(dev, EV_KEY, KEY_WLAN) < 0)
        goto teardown_grab;

    struct libevdev_uinput * udev;
    if(libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED,
                                          &udev) < 0)
        goto teardown_grab;

    for(;;) {
        struct input_event input;
        int                rc = libevdev_next_event(
            dev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
            &input);

        while(rc == LIBEVDEV_READ_STATUS_SYNC)
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);

        if(rc == -EAGAIN) continue;

        if(rc != LIBEVDEV_READ_STATUS_SUCCESS) break;

        struct input_event output[16];
        for(int i = 0, k = eventmap(&input, output); i < k; ++i)
            if(libevdev_uinput_write_event(udev, output[i].type, output[i].code,
                                           output[i].value) < 0)
                goto teardown_udev;
    }

    result = 1;

teardown_udev:
    libevdev_uinput_destroy(udev);
teardown_grab:
    libevdev_grab(dev, LIBEVDEV_UNGRAB);
teardown_dev:
    libevdev_free(dev);
teardown_fd:
    close(fd);

    return result;
}

void eventmap_exec(const char * self_path, const char * devnode) {
    switch(fork()) {
    case -1:
        fprintf(stderr, "Fork failed on %s %s (%s)\n", self_path, devnode,
                strerror(errno));
        break;
    case 0: {
        char * command[] = {(char *)self_path, (char *)devnode, NULL};
        execvp(command[0], command);
        fprintf(stderr, "Exec failed on %s %s (%s)\n", self_path, devnode,
                strerror(errno));
    } break;
    }
}

int should_grab(struct udev_device * device, int initial_scan) {
    if(device == NULL) return 0;

    const char virtual_devices_directory[] = "/sys/devices/virtual/input/";
    if(strncmp(udev_device_get_syspath(device), virtual_devices_directory,
               sizeof(virtual_devices_directory) - 1) == 0)
        return 0;

    if(!initial_scan) {
        const char * action = udev_device_get_action(device);
        if(!action || strcmp(action, "add")) return 0;
    }

    const char   input_prefix[] = "/dev/input/event";
    const char * devnode        = udev_device_get_devnode(device);
    if(!devnode || strncmp(devnode, input_prefix, sizeof(input_prefix) - 1))
        return 0;

    int fd = open(devnode, O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "Failed to open %s (%s)\n", devnode, strerror(errno));
        return 0;
    }

    struct libevdev * dev;
    if(libevdev_new_from_fd(fd, &dev) < 0) {
        fprintf(stderr, "Failed to create evdev device from %s (%s)\n", devnode,
                strerror(errno));
        close(fd);
        return 0;
    }

    int should_be_grabbed =
        libevdev_has_event_type(dev, EV_KEY) &&
        (libevdev_has_event_code(dev, EV_KEY, KEY_ESC) ||
         libevdev_has_event_code(dev, EV_KEY, KEY_CAPSLOCK));

    libevdev_free(dev);
    close(fd);

    return should_be_grabbed;
}

void kill_zombies(__attribute__((unused)) int signum) {
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0)
        ;
}

int main(int argc, const char * argv[]) {
    int initial_scan;

    if(argc > 2) {
        fprintf(stderr, "usage: caps2esc [device-path]\n");
        return EXIT_FAILURE;
    }

    if(argc == 2) return !eventmap_loop(argv[1]);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_flags   = SA_NOCLDSTOP;
    sa.sa_handler = &kill_zombies;
    if(sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Couldn't summon zombie killer");
        return EXIT_FAILURE;
    }

    struct udev * udev = udev_new();
    if(!udev) {
        perror("Can't create udev");
        return EXIT_FAILURE;
    }

    struct udev_enumerate * enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    struct udev_list_entry * devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry * dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices) {
        struct udev_device * device = udev_device_new_from_syspath(
            udev, udev_list_entry_get_name(dev_list_entry));
        if(device) {
            if(should_grab(device, initial_scan = 1))
                eventmap_exec(argv[0], udev_device_get_devnode(device));
            udev_device_unref(device);
        }
    }
    udev_enumerate_unref(enumerate);

    struct udev_monitor * monitor = udev_monitor_new_from_netlink(udev, "udev");
    if(!monitor) {
        perror("Can't create monitor");
        return EXIT_FAILURE;
    }

    udev_monitor_filter_add_match_subsystem_devtype(monitor, "input", NULL);
    udev_monitor_enable_receiving(monitor);
    int fd = udev_monitor_get_fd(monitor);
    for(;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        if(select(fd + 1, &fds, NULL, NULL, NULL) > 0 && FD_ISSET(fd, &fds)) {
            struct udev_device * device = udev_monitor_receive_device(monitor);
            if(device) {
                if(should_grab(device, initial_scan = 0))
                    eventmap_exec(argv[0], udev_device_get_devnode(device));
                udev_device_unref(device);
            }
        }
    }

    udev_monitor_unref(monitor);
    udev_unref(udev);
}
