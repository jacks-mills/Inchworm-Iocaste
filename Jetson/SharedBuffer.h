#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#define SHM_SEGMENT_NAME "MyLidarShmSegment"
#define SHM_OBJECT_NAME "MySharedLidarData"
#define SHM_SIZE_BYTES 65536
#define MAX_LIDAR_POINTS 360

struct LidarPoint {
    float angle; 
    float distance;
};

struct SharedDataBuffer {
    bool is_writing; 
    int total_points; 
    LidarPoint points[MAX_LIDAR_POINTS];
};

#endif
