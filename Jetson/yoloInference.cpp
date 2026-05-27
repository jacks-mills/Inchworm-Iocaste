#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <csignal>
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <opencv2/opencv.hpp>
#include <dirent.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <zmq.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

#include "nlohmann/json.hpp"

#include "SharedBuffer.h" 

#define BRIO100_HFOV 51.5

using namespace boost::interprocess;

const std::string laptop_ip = "100.93.254.108";
const std::string laptop_port = "5555";

std::string base64_encode(const std::vector<uchar>& data) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string ret;
    int i = 0, j = 0;
    uchar char_array_3[3], char_array_4[4];

    for (uchar byte : data) {
        char_array_3[i++] = byte;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; (i <4) ; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
        while((i++ < 3)) ret += '=';
    }
    return ret;
}

// Logger for TensorRT info/warning/errors
class Logger : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* msg) noexcept override
    {
        // Skip info messages
        if (severity == Severity::kINFO) return;
        
        switch (severity)
        {
            case Severity::kINTERNAL_ERROR: std::cerr << "INTERNAL_ERROR: "; break;
            case Severity::kERROR: std::cerr << "ERROR: "; break;
            case Severity::kWARNING: std::cerr << "WARNING: "; break;
            case Severity::kINFO: std::cerr << "INFO: "; break;
            case Severity::kVERBOSE: std::cerr << "VERBOSE: "; break;
            default: std::cerr << "UNKNOWN: "; break;
        }
        std::cerr << msg << std::endl;
    }
};

// Destroy TensorRT objects
struct TRTDestroy
{
    template <class T>
    void operator()(T* obj) const
    {
        if (obj)
            obj->destroy();
    }
};

// Signal handler for clean signal
volatile sig_atomic_t signal_received = 0;

void sig_handler(int signo)
{
	if (signo == SIGINT) {
		std::cout << "Received SIGINT" << std::endl;
		signal_received = 1;
	}
}

// YOLO Inference class
struct Detection {
    int classId;
    float confidence;
    cv::Rect box;
};

double getAngleFromCenter(int targetX, int imageWidth, double hFOV)
{
	double cx = imageWidth / 2.0;
	double hFOV_rad = hFOV * M_PI / 180.0;
	double fx = cx /std::tan(hFOV_rad / 2.0);

    double deltaX = targetX - cx;

    double horizontalAngle = std::atan2(deltaX, fx) * 180.0 / M_PI;

    return horizontalAngle;
}


class YOLOInference
{
private:
    Logger logger;
    std::unique_ptr<nvinfer1::ICudaEngine, TRTDestroy> engine;
    std::unique_ptr<nvinfer1::IExecutionContext, TRTDestroy> context;
    cudaStream_t stream;
    
    // Input and output buffer pointers
    void* buffers[2]; // Assuming one input, one output
    int inputIndex;
    int outputIndex;
    size_t inputSize;
    size_t outputSize;
    nvinfer1::Dims inputDims;
    nvinfer1::Dims outputDims;
    
    //FPS calculation 
    float networkFPS;
    std::chrono::steady_clock::time_point lastTime;

public:
    YOLOInference(const std::string& engineFile) : stream(nullptr)
    {
        // Start loading the engine
        std::cout << "Loading TensorRT engine: " << engineFile << std::endl;

        // Opens the file whose path is engineFile for reading in binary mode. 
        std::ifstream file(engineFile, std::ios::binary);

        // Checks if the stream opened successfully. If not it throws an execption.
        if (!file.good()) {
            throw std::runtime_error("Failed to open engine file: " + engineFile);
        }
        
        // Moves the file pointer to the end of the file.
        file.seekg(0, std::ios::end);

        // Calls tellg to determine the number of bytes from file start which is the file size
        size_t size = file.tellg();

        // Moves the file pointer back to the beginning of the file
        file.seekg(0, std::ios::beg);
        
        // Allocates a buffer to store the binary from the engine file
        std::vector<char> engineData(size);
        // Reads the file into the buffer
        file.read(engineData.data(), size);
        
        // Checks the stream state after the read, if failed throws exception.
        if (!file) {
            throw std::runtime_error("Failed to read engine file");
        }
        
        // Creates an IRuntime pointer and wraps it in a unique ptr that uses TRTDestroy functor to call 
        // destroy when the pointer is freed. The logger is passed so TensorRT can report messages. 
        // 
        std::unique_ptr<nvinfer1::IRuntime, TRTDestroy> runtime(nvinfer1::createInferRuntime(logger));
        // Uses the runtime to deserialize the raw engine bytes from engineData into a live ICudaEngine*. 
        // Returned pointer replaces the engine managed unique_ptr 
        engine.reset(runtime->deserializeCudaEngine(engineData.data(), size));
        
        // If the pointer to the deserialized execution plan is NULL, throw an exception
        if (!engine) {
            throw std::runtime_error("Failed to deserialize engine");
        }
        
        // Create execution context which holds runtime state required to run the plan. 
        context.reset(engine->createExecutionContext());
        // If NULL an error has occured, throw an exception.
        if (!context) {
            throw std::runtime_error("Failed to create execution context");
        }
        
        // Create CUDA stream
        cudaError_t err = cudaStreamCreate(&stream);
        if (err != cudaSuccess) {
            throw std::runtime_error("Failed to create CUDA stream");
        }
        
        // Find input and output binding indices
        inputIndex = -1;
        outputIndex = -1;
        
        // Loops through bindings and finds if the binding is an input or output 
        // Getting dimensions and indices
        for (int i = 0; i < engine->getNbBindings(); i++) {
            if (engine->bindingIsInput(i)) {
                inputIndex = i;
                inputDims = engine->getBindingDimensions(i);
            } else {
                outputIndex = i;
                outputDims = engine->getBindingDimensions(i);
            }
        }

        // If no input or output binding throw an exception
        if (inputIndex == -1 || outputIndex == -1) {
            throw std::runtime_error("Could not find input or output binding");
        }
        
        // Calculate sizes of memory for input and output (floats)
        inputSize = 1;
        for (int i = 0; i < inputDims.nbDims; i++) {
            inputSize *= inputDims.d[i];
        }
        inputSize *= sizeof(float);
        
        outputSize = 1;
        for (int i = 0; i < outputDims.nbDims; i++) {
            outputSize *= outputDims.d[i];
        }
        outputSize *= sizeof(float);
        
        // Allocate GPU memory
        cudaMalloc(&buffers[inputIndex], inputSize);
        cudaMalloc(&buffers[outputIndex], outputSize);
        
        std::cout << "TensorRT engine loaded successfully" << std::endl;
        std::cout << "Input shape: ";
        for (int i = 0; i < inputDims.nbDims; i++) {
            std::cout << inputDims.d[i] << " ";
        }
        std::cout << std::endl;
        
        std::cout << "Output shape: ";
        for (int i = 0; i < outputDims.nbDims; i++) {
            std::cout << outputDims.d[i] << " ";
        }
        std::cout << std::endl;

        //Initialise FPS timer 
        lastTime = std::chrono::steady_clock::now();
    }
    
    ~YOLOInference()
    {
        // Free allocated resources
        if (buffers[inputIndex]) cudaFree(buffers[inputIndex]);
        if (buffers[outputIndex]) cudaFree(buffers[outputIndex]);
        if (stream) cudaStreamDestroy(stream);
    }
    
    // Process a single image and return detections
    std::vector<Detection> processImage(const cv::Mat& image)
    {
	    //Start timer for FPS calculation
	    auto startTime = std::chrono::steady_clock::now();

        // Get dimensions
        const int batchSize = inputDims.d[0];
        const int channels = inputDims.d[1];
        const int height = inputDims.d[2];
        const int width = inputDims.d[3];
        
        // Resize and preprocess image
        cv::Mat resized;
        cv::resize(image, resized, cv::Size(width, height));
        
        // Convert to grayscale if needed
        cv::Mat gray;
        if (channels == 1 && resized.channels() == 3) {
            cv::cvtColor(resized, gray, cv::COLOR_BGR2GRAY);
        } else if (channels == 3 && resized.channels() == 1) {
            cv::cvtColor(resized, gray, cv::COLOR_GRAY2BGR);
        } else {
            gray = resized;
        }
        
        // Normalize to [0,1]
        cv::Mat normalized;
        gray.convertTo(normalized, CV_32F, 1.0/255.0);
        
        // Allocate host memory for input
        std::vector<float> inputData(inputSize / sizeof(float));
        
        // Copy data to input buffer (assuming NCHW format)
        if (channels == 1) {
            // For grayscale, just copy the data
            for (int h = 0; h < height; h++) {
                for (int w = 0; w < width; w++) {
                    inputData[h * width + w] = normalized.at<float>(h, w);
                }
            }
        } else {
            // For color, copy each channel
            for (int c = 0; c < channels; c++) {
                for (int h = 0; h < height; h++) {
                    for (int w = 0; w < width; w++) {
                        inputData[(c * height * width) + (h * width) + w] = 
                            normalized.at<cv::Vec3f>(h, w)[c];
                    }
                }
            }
        }
        
        // Copy input data to GPU
        cudaMemcpy(buffers[inputIndex], inputData.data(), inputSize, cudaMemcpyHostToDevice);
        
        // Execute inference
        if (!context->enqueueV2(buffers, stream, nullptr)) {
            throw std::runtime_error("Failed to execute inference");
        }
        
        // Allocate host memory for output
        std::vector<float> outputData(outputSize / sizeof(float));
        
        // Copy output back to host
        cudaMemcpy(outputData.data(), buffers[outputIndex], outputSize, cudaMemcpyDeviceToHost);
        
        // Synchronize stream
        cudaStreamSynchronize(stream);

        // Parse outputData (host float vector) into proposals.
        // Determine signal (channels per proposal) and stride (number of proposals) from outputDims
        int signalResultNum = 0;
        int strideNum = 0;
        if (outputDims.nbDims == 3) {
            // Common shape: [batch, C, N]
            signalResultNum = outputDims.d[1];
            strideNum = outputDims.d[2];
        } else if (outputDims.nbDims == 2) {
            // [C, N]
            signalResultNum = outputDims.d[0];
            strideNum = outputDims.d[1];
        } else {
            throw std::runtime_error("Unsupported outputDims.nbDims: " + std::to_string(outputDims.nbDims));
        }

        if (signalResultNum <= 4 || strideNum <= 0) {
            throw std::runtime_error("Unexpected output dimensions from engine");
        }

        // Wrap flat output into Mat (C x N) then transpose so each row is a proposal
        cv::Mat rawData(signalResultNum, strideNum, CV_32F, outputData.data());
        rawData = rawData.t(); // now rows==strideNum, cols==signalResultNum

        float* data = reinterpret_cast<float*>(rawData.data);

        int numClasses = signalResultNum - 4; // assume first 4 are bbox
        if (numClasses <= 0) numClasses = 1;

        std::vector<int> class_ids;
        std::vector<float> confidences;
        std::vector<cv::Rect> boxes;

        // Input resize mapping: we resized original to (width, height) earlier
        float x_scale = static_cast<float>(image.cols) / static_cast<float>(width);
        float y_scale = static_cast<float>(image.rows) / static_cast<float>(height);

        const float confThreshold = 0.25f;
        const float nmsThreshold = 0.45f;

        for (int i = 0; i < strideNum; ++i) {
            float* classesScores = data + 4;
            cv::Mat scores(1, numClasses, CV_32FC1, classesScores);
            cv::Point class_id;
            double maxClassScore;
            cv::minMaxLoc(scores, 0, &maxClassScore, 0, &class_id);
            if (maxClassScore > confThreshold) {
                confidences.push_back(static_cast<float>(maxClassScore));
                class_ids.push_back(class_id.x);

                float cx = data[0];
                float cy = data[1];
                float wbox = data[2];
                float hbox = data[3];

                int left = static_cast<int>((cx - 0.5f * wbox) * x_scale);
                int top = static_cast<int>((cy - 0.5f * hbox) * y_scale);
                int box_w = static_cast<int>(wbox * x_scale);
                int box_h = static_cast<int>(hbox * y_scale);

                // clip
                left = std::max(0, std::min(left, image.cols - 1));
                top = std::max(0, std::min(top, image.rows - 1));
                box_w = std::max(0, std::min(box_w, image.cols - left));
                box_h = std::max(0, std::min(box_h, image.rows - top));

                boxes.emplace_back(left, top, box_w, box_h);
            }
            data += signalResultNum;
        }

        // Run NMS
        std::vector<int> nmsResult;
        if (!boxes.empty()) {
            cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, nmsResult);
        }

        std::vector<Detection> results;
        for (int idx : nmsResult) {
            Detection d;
            d.classId = class_ids[idx];
            d.confidence = confidences[idx];
            d.box = boxes[idx];
            results.push_back(d);
        }

	//Update FPS calculation
	auto endTime = std::chrono::steady_clock::now();

	//Calculate time in ms 
	float ms = std::chrono::duration<float, std::milli>(endTime - startTime).count();

	//Update running average of networ time
	networkFPS = 1000.0f / ms;

        return results;
    }

    float GetNetworkFPS() const { return networkFPS; }
};

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <engine_file>" << std::endl;
        return 1;
    }
    
    std::string engineFile = argv[1];

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
	    std::cerr << "Can't catch SIGINT" << std::endl;
	    return 1;
    }

    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_PUSH);

    std::string connection_address = "tcp://" + laptop_ip + ":" + laptop_port;

    socket.connect(connection_address);

    int serial_port = open("/dev/ttyACM0", O_RDWR | O_NOCTTY); 
    if (serial_port < 0) {
        std::cerr << "Warning: Could not open /dev/ttyACM0 (Microcontroller offline or permissions issue: " 
                  << strerror(errno) << "). Continuing stream layout anyhow..." << std::endl;
    } else {
        struct termios tty;
        if (tcgetattr(serial_port, &tty) == 0) {
            cfsetospeed(&tty, B115200); // Fast baud alignment matching your microcontroller configuration
            cfsetispeed(&tty, B115200);
            tty.c_cflag &= ~PARENB;        tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CSIZE;         tty.c_cflag |= CS8;
            tty.c_cflag &= ~CRTSCTS;       tty.c_cflag |= CREAD | CLOCAL;
            tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
            tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT);
            tty.c_oflag &= ~OPOST;
            tcsetattr(serial_port, TCSANOW, &tty);
            std::cout << "Hardware USB-UART controller pipeline established at 115200 Baud." << std::endl;
        }
    }

    cv::VideoCapture cap(
		    "v4l2src device=/dev/video0 ! "
		    "image/jpeg, width=960, height=720, framerate=30/1 ! "
		    "jpegdec ! videoconvert ! appsink",
		    cv::CAP_GSTREAMER);

    if (!cap.isOpened()){
	    std::cerr << "Error: Could not open camera" << std::endl;
	    return -1;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, 960);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_FPS, 30);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    std::cout << "Camera opened successfully" << std::endl;
    
    SharedDataBuffer* shared_lidar = nullptr;
    std::unique_ptr<managed_shared_memory> shm_segment;

    try {
        // Open the existing segment created by your running LiDAR executable 
        shm_segment = std::make_unique<managed_shared_memory>(open_only, SHM_SEGMENT_NAME);

        // Find unique structural instance inside opened memory 
        std::pair<SharedDataBuffer*, managed_shared_memory::size_type> res = 
            shm_segment->find<SharedDataBuffer>(SHM_OBJECT_NAME);

        if (res.second == 1) {
            shared_lidar = res.first;
            std::cout << "Successfully linked into active LiDAR Shared Memory Segment!" << std::endl;
        } else {
            std::cerr << "LiDAR instance structure object not found in memory." << std::endl;
            return 1;
        }
    } catch(const std::exception& e) {
        std::cerr << "Could not open shared memory. Make sure LiDAR process is running first!" << e.what() << std::endl;
        return 1;
    }

    try {
        // Initialize CUDA
        cudaSetDevice(0);
        
        // Create inference object
        YOLOInference inference(engineFile);

        static const std::vector<std::string> cocoNames = { "person" };

        cv::Mat frame;
        // Process each image
        while(!signal_received) {
		    cap >> frame;
            if (frame.empty()) {
                std::cerr << "Failed to read image" << std::endl;
                continue;
            }
            
            std::vector<double> distances; 
            double obstacle_angle = 0.0;
            float obstacle_distance = -1.0f;
            bool should_brake = false;

            // Run inference and get detections
            std::vector<Detection> dets = inference.processImage(frame);
            
            for (const auto &d : dets) {
                cv::rectangle(frame, d.box, cv::Scalar(0, 255, 0), 2);

                int centerX = d.box.x + (d.box.width / 2);

                double hAngle = getAngleFromCenter(centerX, frame.cols, BRIO100_HFOV);

                float found_distance = -1.0f;

                if (shared_lidar && !shared_lidar->is_writing) {
                    float best_delta = 999.0f;
                    const float matching_tolerance = 1.5f;

                    for (int i = 0; i < shared_lidar->total_points; ++i) {
                        float current_delta = std::abs(shared_lidar->points[i].angle - hAngle);
                        if (current_delta > 180.0f) current_delta = 360.0f - current_delta;

                        if (current_delta <= matching_tolerance && current_delta < best_delta) {
                            best_delta = current_delta; 
                            found_distance = shared_lidar->points[i].distance;
                        }
                    }
                }

		fprintf(stdout, "Found distance is %f\n", found_distance);

                distances.push_back(found_distance);

                if (found_distance > 0.0f) {
                    obstacle_distance = found_distance; 
                    obstacle_angle = hAngle; 
                    should_brake = true;
                }

                std::ostringstream label;
                if (d.classId >= 0 && d.classId < static_cast<int>(cocoNames.size())) {
                    label << cocoNames[d.classId] << ":" << std::fixed << std::setprecision(2) << d.confidence << "- Horizontal Angle: " << hAngle << found_distance << "mm";
                } else {
                    label << "id:" << d.classId << ":" << std::fixed << std::setprecision(2) << d.confidence;
                }

                int baseLine = 0;
                cv::Size labelSize = cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
                int top = std::max(d.box.y, labelSize.height);
                cv::rectangle(frame, cv::Point(d.box.x, top - labelSize.height), cv::Point(d.box.x + labelSize.width, top + baseLine), cv::Scalar(0, 255, 0), cv::FILLED);
                cv::putText(frame, label.str(), cv::Point(d.box.x, top), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,0), 1);
            }

            std::string fpsText = "FPS: " + std::to_string(static_cast<int>(inference.GetNetworkFPS()));
            cv::putText(frame, fpsText, cv::Point(20,40), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,0,255), 2);
            cv::imshow("YOLO Live Stream", frame);

            if (should_brake && serial_port >= 0) {
                nlohmann::json brake_msg;
                brake_msg["message_type"] = "brake request";
                brake_msg["sender"] = "jetson";
                brake_msg["percentage"] = 100;

                // Output includes standard '\n' required by your non-threaded Zephyr sieve parser!
                std::string brake_str = brake_msg.dump() + "\n";
                write(serial_port, brake_str.c_str(), brake_str.size());
            }

            std::vector<uchar> buf;
            cv::imencode(".jpg", frame, buf);
	    
	    std::string b64_image = base64_encode(buf);

            nlohmann::json json_payload;

            json_payload["message_type"] = "image";
            json_payload["image_data"] = b64_image;
            json_payload["width"] = frame.cols;
            json_payload["height"] = frame.rows;
            json_payload["distance"] = obstacle_distance;
            json_payload["angle"] = obstacle_angle;
            

            std::string json_string = json_payload.dump();

            zmq::message_t request(json_string.size());

            memcpy(request.data(), json_string.c_str(), json_string.size());

            socket.send(request, ZMQ_DONTWAIT);

            if (cv::waitKey(1) == 'q') {
                break;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        cap.release();
        cv::destroyAllWindows();
        return 1;
    }

    if (serial_port >= 0) close(serial_port);
    cap.release();
    cv::destroyAllWindows();
    
    return 0;
} 
