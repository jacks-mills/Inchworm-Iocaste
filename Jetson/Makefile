CXX = g++
CXXFLAGS = -std=c++14 -O2 -Wall -pthread

# Project Files
SHARED_HDR = SharedBuffer.h
LIDAR_SRC  = Lidar.cpp
YOLO_SRC   = yoloInference.cpp

# Target Executables
LIDAR_TARGET = lidar_grabber
YOLO_TARGET  = yolo_inference

# Include Paths
# Maps local project headers, local SDK structures, CUDA, and TensorRT
INCLUDES = -I. \
           -Isdk/include \
           -Isdk/src \
           -I/usr/local/cuda/include \
           -I/usr/include/aarch64-linux-gnu

# Library Dependencies & Linking Flags
# Resolve OpenCV flags dynamically via pkg-config
OPENCV_CFLAGS = $(shell pkg-config --cflags opencv4)
OPENCV_LIBS   = $(shell pkg-config --libs opencv4)

# Local static LiDAR SDK library path
LIDAR_STATIC_LIB = lib/libsl_lidar_sdk.a

# Final Linking flags for Lidar (requires POSIX Real-time -lrt and -lpthread)
LIDAR_LDFLAGS = $(LIDAR_STATIC_LIB) -lrt -lpthread

# Final Linking flags for YOLO (CUDA, TensorRT, ZeroMQ, and OpenCV)
YOLO_LDFLAGS = -L/usr/local/cuda/lib64 \
               -lcudart \
               -lnvinfer \
               -lnvonnxparser \
               -lzmq \
               -lrt \
               -lpthread \
               $(OPENCV_LIBS)


.PHONY: all clean

# Default rule builds both executables
all: $(LIDAR_TARGET) $(YOLO_TARGET)

# Compile Lidar Grabber
$(LIDAR_TARGET): $(LIDAR_SRC) $(SHARED_HDR)
	@echo "Building Standalone LiDAR Driver..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIDAR_SRC) -o $(LIDAR_TARGET) $(LIDAR_LDFLAGS)
	@echo "Successfully compiled: $(LIDAR_TARGET)"

# Compile YOLO Inference Engine
$(YOLO_TARGET): $(YOLO_SRC) $(SHARED_HDR)
	@echo "Building YOLO Inference Engine..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OPENCV_CFLAGS) $(YOLO_SRC) -o $(YOLO_TARGET) $(YOLO_LDFLAGS)
	@echo "Successfully compiled: $(YOLO_TARGET)"

# Clean up build artifacts
clean:
	@echo "Cleaning up build targets..."
	rm -f $(LIDAR_TARGET) $(YOLO_TARGET)
	@echo "Done."
