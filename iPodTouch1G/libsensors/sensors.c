/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#define LOG_TAG "sensors"
#define SENSORS_SERVICE_NAME "sensors"

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/input.h>
#include <sys/select.h>

#include <hardware/sensors.h>
#include <cutils/native_handle.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>

/******************************************************************************/
#define ID_BASE SENSORS_HANDLE_BASE
#define ID_ACCELERATION (ID_BASE+0)

#define CONVERT                 (GRAVITY_EARTH/64.0f)
#define SENSORS_ACCELERATION    (1 << ID_ACCELERATION)
#define INPUT_DIR               "/dev/input"
#define ACCEL_SYS_DIR		"/sys/devices/i2c-0/0-003a/input"
#define SUPPORTED_SENSORS       (SENSORS_ACCELERATION)
#define EVENT_MASK_ACCEL_ALL    ( (1 << ABS_X) | (1 << ABS_Y) | (1 << ABS_Z))
#define DEFAULT_THRESHOLD 100

#define ACCELERATION_X (1 << ABS_X)
#define ACCELERATION_Y (1 << ABS_Y)
#define ACCELERATION_Z (1 << ABS_Z)
#define SENSORS_ACCELERATION_ALL (ACCELERATION_X | ACCELERATION_Y | \
        ACCELERATION_Z)
#define SEC_TO_NSEC 1000000000LL
#define USEC_TO_NSEC 1000
#define CONTROL_READ 0
#define CONTROL_WRITE 1
#define WAKE_SOURCE 0x1a

uint32_t active_sensors;
int sensor_fd = -1;
int event_fd = -1;
int control_fd[2] = { -1, -1 };


/*
 * the following is the list of all supported sensors
 */
static const struct sensor_t apple_sensor_list[] =
{
    {
        .name = "LIS331DL 3-axis Accelerometer",
        .vendor = "STMicroelectronics",
        .version = 1,
        .handle =SENSOR_TYPE_ACCELEROMETER,
        .type = SENSOR_TYPE_ACCELEROMETER,
        .maxRange = (GRAVITY_EARTH * 2.3f),
        .resolution = (GRAVITY_EARTH * 2.3f) / 128.0f,
        .power = 0.4f,  //power consumption max is rated max 0.3mA...
        .reserved = {},
     },
};

static uint32_t sensors_get_list(struct sensors_module_t *module,
                                 struct sensor_t const** list)
{
    *list = apple_sensor_list;
    return 1;
}

/** Close the sensors device */
static int
close_sensors(struct hw_device_t *dev)
{
    struct sensors_data_device_t *device_data =
                    (struct sensors_data_device_t *)dev;
    if (device_data) {
        if (event_fd > 0)
            close(event_fd);
        free(device_data);
    }
    return 0;
}

int open_sensors_phy(struct sensors_control_device_t *dev)
{
    char devname[PATH_MAX];
    char *filename;
    int fd;
    int res;
    uint8_t bits[4];
    DIR *dir;
    DIR *sys_dir;
    struct dirent *de;


    dir = opendir(INPUT_DIR);
    if (dir == NULL)
        return -1;

    sys_dir = opendir(ACCEL_SYS_DIR);
    if (dir == NULL)
        return -1;


    strcpy(devname, INPUT_DIR);
    filename = devname + strlen(devname);
    strcpy(filename, "/event");

    while ((de = readdir(sys_dir)))
    {
        if (de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                 (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
	filename = devname + strlen(devname);
	*filename++ = de->d_name[5];
        fd = open(devname, O_RDONLY);
        if (fd < 0)
        {
            LOGE("Accelerometer: Couldn't open %s, error = %d", devname, fd);
            continue;
        }
        res = ioctl(fd, EVIOCGBIT(EV_ABS, 4), bits);
        if (res <= 0 || bits[0] != EVENT_MASK_ACCEL_ALL)
        {
            close(fd);
            continue;
        }

        closedir(dir);
        return fd;
    }
    closedir(dir);

    return -1;
}

static native_handle_t *control_open_data_source(struct sensors_control_device_t *dev)
{
    native_handle_t *hd;
    if (control_fd[0] == -1 && control_fd[1] == -1) {
        if (socketpair(AF_LOCAL, SOCK_STREAM, 0, control_fd) < 0 )
        {
            LOGE("Accelerometer: could not create thread control socket pair: %s",
                 strerror(errno));
            return NULL;
        }
    }

    sensor_fd = open_sensors_phy(dev);

    hd = native_handle_create(1, 0);
    hd->data[0] = sensor_fd;

    return hd;
}

static int control_activate(struct sensors_control_device_t *dev,
                            int handle, int enabled)
{
    uint32_t mask = (1 << handle);
    uint32_t sensors;
    uint32_t new_sensors, active, changed;

    sensors = enabled ? mask : 0;
    active = active_sensors;
    new_sensors = (active & ~mask) | (sensors & mask);
    changed = active ^ new_sensors;
    if (!changed)
        return 0;

    active_sensors = new_sensors;

    if (!enabled)
    {
        LOGD("Accelerometer: Deactivating Accelerometer sensor\n");
    }
    else
    {
        LOGD("Accelerometer: Activating Accelerometer sensor\n");
    }

    return 0;
}

static int control_set_delay(struct sensors_control_device_t *dev, int32_t ms)
{
    return 0;
}

static int control_wake(struct sensors_control_device_t *dev)
{
    int err = 0;
    if (control_fd[CONTROL_WRITE] > 0) 
    {
	 struct input_event event[1];

	 event[0].type = EV_SYN;
         event[0].code = SYN_CONFIG;
         event[0].value = 0;
         err = write(control_fd[CONTROL_WRITE], event, sizeof(event));
         LOGD_IF(err<0, "control__wake, err=%d (%s)", errno, strerror(errno));
    }

    return err;
}



int sensors_open(struct sensors_data_device_t *dev, native_handle_t* hd)
{
    event_fd = dup(hd->data[0]);

    native_handle_close(hd);
    native_handle_delete(hd);

    return 0;
}
static int control_close(struct hw_device_t *dev)
{
    struct sensors_control_device_t *device_control = (void *) dev;
    close(control_fd[CONTROL_WRITE]);
    close(control_fd[CONTROL_READ]);
    close(sensor_fd);
    sensor_fd = -1; 
    control_fd[0] = -1; 
    control_fd[1] = -1;
    return 0;
}
int sensors_close(struct sensors_data_device_t *dev)
{
    if (event_fd > 0) {
        close(event_fd);
        event_fd = -1;
    }
    return 0;
}

int sensors_poll(struct sensors_data_device_t *dev, sensors_data_t* sensors)
{
    int fd = event_fd;
    fd_set rfds;
    struct input_event ev;
    int ret;
    uint32_t new_sensors = 0;
    int select_dim;

    sensors->vector.status = SENSOR_STATUS_ACCURACY_HIGH;
    select_dim = (fd > control_fd[CONTROL_READ]) ?
                 fd + 1 : control_fd[CONTROL_READ] + 1;
    while (1)
    {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        FD_SET(control_fd[CONTROL_READ], &rfds);

        do {
            ret = select(select_dim, &rfds, NULL, NULL, 0);
        } while (ret < 0 && errno == EINTR);

        if (FD_ISSET(control_fd[CONTROL_READ], &rfds))
        {
            char ch;
            read(control_fd[CONTROL_READ], &ch, sizeof(ch));
	    if (ch==WAKE_SOURCE)
	    {
		    FD_ZERO(&rfds);
	            return -EWOULDBLOCK;
	    }
        }

        ret = read(fd, &ev, sizeof(ev));
        if (ret < (int)sizeof(ev))
            break;

        FD_CLR(control_fd[CONTROL_READ], &rfds);

        if (ev.type == EV_ABS)
        {
            /* Orientation or acceleration event */
            switch (ev.code)
            {
            case ABS_X:
                new_sensors |= ACCELERATION_X;
                sensors->acceleration.x = (ev.value * CONVERT );
                break;
            case ABS_Y:
                new_sensors |= ACCELERATION_Y;
                sensors->acceleration.y = (ev.value * CONVERT);
                break;
            case ABS_Z:
                new_sensors |= ACCELERATION_Z;
                sensors->acceleration.z = (ev.value * CONVERT);
                break;
            }
        }
	else
		return 0;
	if (new_sensors == (ACCELERATION_X | ACCELERATION_Y | ACCELERATION_Z))
	{
	    	sensors->sensor = SENSOR_TYPE_ACCELEROMETER;
		return SENSOR_TYPE_ACCELEROMETER;
	}
    }
    return 0;
}

/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a sensors device using name */
static int open_sensors(const struct hw_module_t* module, char const* name,
                        struct hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, SENSORS_HARDWARE_CONTROL))
    {
        struct sensors_control_device_t *device_control =
                        malloc(sizeof(struct sensors_control_device_t));
        memset(device_control, 0, sizeof(*device_control));

        device_control->common.tag = HARDWARE_DEVICE_TAG;
        device_control->common.version = 0;
        device_control->common.module = (struct hw_module_t*)module;
        device_control->common.close = control_close;
        device_control->open_data_source = control_open_data_source;
        device_control->activate = control_activate;
        device_control->set_delay = control_set_delay;
        device_control->wake = control_wake;
        sensor_fd = -1;
        *device = &device_control->common;
        status = 0;
     } else if (!strcmp(name, SENSORS_HARDWARE_DATA)) {
        struct sensors_data_device_t *device_data =
                        malloc(sizeof(struct sensors_data_device_t));
        memset(device_data, 0, sizeof(*device_data));

        device_data->common.tag = HARDWARE_DEVICE_TAG;
        device_data->common.version = 0;
        device_data->common.module = (struct hw_module_t*)module;
        device_data->common.close = close_sensors;
        device_data->data_open = sensors_open;
        device_data->data_close = sensors_close;
        device_data->poll = sensors_poll;
        event_fd = -1;
        *device = &device_data->common;
        status = 0;
    }
    return status;
}

static struct hw_module_methods_t sensors_module_methods =
{
    .open =  open_sensors,
};

/*
 * The Sensors Hardware Module
 */
const struct sensors_module_t HAL_MODULE_INFO_SYM =
{
    .common = {
        .tag           = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id            = SENSORS_HARDWARE_MODULE_ID,
        .name          = "LIS331DL accelerometer sensors Module",
        .author        = "Dario Russo (turbominchiameister@gmail.com)",
        .methods       = &sensors_module_methods,
    },
    .get_sensors_list = sensors_get_list
};
