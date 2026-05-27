#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include "sl_lidar.h" 
#include "sl_lidar_driver.h"

#include "SharedBuffer.h"

#ifndef _countof
#define _countof(_Array) (int)(sizeof(_Array) / sizeof(_Array[0]))
#endif

#define BRIO100_HFOV 51.5f
#define FOV_HALF (BRIO100_HFOV / 2.0f)
#define MIN_BOUND (360.0f - FOV_HALF)
#define MAX_BOUND (FOV_HALF)

using namespace sl;
using namespace boost::interprocess;

bool checkSLAMTECLIDARHealth(ILidarDriver * drv)
{
    sl_result op_result;
    sl_lidar_response_device_health_t healthinfo;

    op_result = drv->getHealth(healthinfo);
    if (SL_IS_OK(op_result)) { // the macro IS_OK is the preperred way to judge whether the operation is succeed.
        printf("SLAMTEC Lidar health status : %d\n", healthinfo.status);
        if (healthinfo.status == SL_LIDAR_STATUS_ERROR) {
            fprintf(stderr, "Error, slamtec lidar internal error detected. Please reboot the device to retry.\n");
            drv->reset();
            return false;
        } 
        
        return true;

    } else {
        fprintf(stderr, "Error, cannot retrieve the lidar health code: %x\n", op_result);
        return false;
    }
}

bool ctrl_c_pressed = false;

void ctrlc(int)
{
    ctrl_c_pressed = true;
}

void print_usage(int argc, const char * argv[])
{
    printf("Usage:\n"
           "  %s <com port> [baudrate]\n"
           "Example:\n"
           "  %s /dev/ttyUSB0 256000\n", argv[0], argv[0]);
}

int main(int argc, const char * argv[]) {
	const char * opt_com_path = NULL;
    sl_u32 opt_baudrate = 0;
    sl_u32 baudrateArray[2] = {115200, 256000};
    sl_result op_result;
	bool useArgcBaudrate = false;
    IChannel* _channel = nullptr;

	if (argc>1) { 
		opt_com_path = argv[1];
	} else {
		print_usage(argc, argv);
		return -1;
	}

    if (argc > 2) {
        opt_baudrate = strtoul(argv[2], NULL, 10);
        useArgcBaudrate = true;
    }

    // Clear old shared memory 
    shared_memory_object::remove(SHM_SEGMENT_NAME);

    // Create shared memory segment
    managed_shared_memory segment(create_only, SHM_SEGMENT_NAME, SHM_SIZE_BYTES);

    // Construct our structural layout directly inside the shared segment 
    SharedDataBuffer* shared_data = segment.construct<SharedDataBuffer>(SHM_OBJECT_NAME)();

    // Initialize shared data struct
    shared_data->is_writing = false;
    shared_data->total_points = 0;

    // create the driver instance
	ILidarDriver * drv = *createLidarDriver();
    if (!drv) {
        fprintf(stderr, "insufficent memory, exit\n");
        exit(-2);
    }

    sl_lidar_response_device_info_t devinfo;
    bool connectSuccess = false;

    if(useArgcBaudrate){
        _channel = (*createSerialPortChannel(opt_com_path, opt_baudrate));
        if (SL_IS_OK((drv)->connect(_channel))) {
            op_result = drv->getDeviceInfo(devinfo);

            if (SL_IS_OK(op_result)) connectSuccess = true;
        }
    } else {
        size_t baudRateArraySize = _countof(baudrateArray);
        for(size_t i = 0; i < baudRateArraySize; ++i) 
        {
            _channel = (*createSerialPortChannel(opt_com_path, baudrateArray[i]));
            if (SL_IS_OK((drv)->connect(_channel))) {
                op_result = drv->getDeviceInfo(devinfo);
                if (SL_IS_OK(op_result)) {
                    connectSuccess = true;
                    break;
                }
            }
        }
    }


    if (!connectSuccess) {
		fprintf(stderr, "Error, cannot bind to the specified serial port %s.\n", opt_com_path);
        if (drv) delete drv;
        return -1;
    }

    // check health...
    if (!checkSLAMTECLIDARHealth(drv)) {
        delete drv;
        return -1;
    }

    signal(SIGINT, ctrlc);
    
    drv->setMotorSpeed();
    drv->startScan(0,1);

    // fetech result and print it out...
    while (!ctrl_c_pressed) {
        sl_lidar_response_measurement_node_hq_t nodes[8192];
        size_t   count = _countof(nodes);

        op_result = drv->grabScanDataHq(nodes, count);

        if (SL_IS_OK(op_result)) {
            drv->ascendScanData(nodes, count);

            shared_data->is_writing = true;
            int saved_count = 0;

            for (int pos = 0; pos < (int)count && saved_count < MAX_LIDAR_POINTS; ++pos) {
                float current_angle = (nodes[pos].angle_z_q14 * 90.f) / 16384.f;
                float current_distance = nodes[pos].dist_mm_q2 / 4.0f;

                if (current_distance > 0.0f) {

                    if (current_angle <= MAX_BOUND || current_angle >= MIN_BOUND) {
			fprintf(stdout, "At angle %f distance is %f\n", current_angle, current_distance);
                        shared_data->points[saved_count].angle = current_angle;
                        shared_data->points[saved_count].distance = current_distance;
                        saved_count++;
                    }
                }
            }

            shared_data->total_points = saved_count;
            shared_data->is_writing = false;
        }
    }

    shared_memory_object::remove(SHM_SEGMENT_NAME);
    drv->stop();
	usleep(200000);
    drv->setMotorSpeed(0);

    delete drv;
    return 0;
}

