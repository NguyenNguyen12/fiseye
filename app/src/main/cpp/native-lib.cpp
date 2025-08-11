#include <jni.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <android/log.h>

#define LOG_TAG "NativeLib"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#pragma pack(push, 1)
struct BMPHeader {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};
struct BMPInfoHeader {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

bool save_pixels_as_bmp(const char* filename, const unsigned char* pixels, int width, int height, int channels) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        LOGE("Failed to open BMP file for writing: %s", filename);
        return false;
    }

    int row_stride = (width * 3 + 3) & ~3;
    uint32_t image_size = row_stride * height;

    BMPHeader header;
    header.bfType = 0x4D42;
    header.bfSize = sizeof(BMPHeader) + sizeof(BMPInfoHeader) + image_size;
    header.bfReserved1 = 0;
    header.bfReserved2 = 0;
    header.bfOffBits = sizeof(BMPHeader) + sizeof(BMPInfoHeader);

    BMPInfoHeader info;
    info.biSize = sizeof(BMPInfoHeader);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biCompression = 0;
    info.biSizeImage = image_size;
    info.biXPelsPerMeter = 0;
    info.biYPelsPerMeter = 0;
    info.biClrUsed = 0;
    info.biClrImportant = 0;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info), sizeof(info));

    std::vector<uint8_t> row_buffer(row_stride);
    for (int y = 0; y < height; ++y) {
        const unsigned char* src_row = pixels + (height - 1 - y) * width * channels; // Read from bottom for BMP
        for (int x = 0; x < width; ++x) {
            // RGBA should work???? dont put grayscale
                row_buffer[x * 3 + 2] = src_row[x * channels + 0]; // r
                row_buffer[x * 3 + 1] = src_row[x * channels + 1]; // g
                row_buffer[x * 3 + 0] = src_row[x * channels + 2]; // b


        }
        file.write(reinterpret_cast<const char*>(row_buffer.data()), row_stride);
    }

    if (!file) {
        LOGE("Failed to write BMP data to file: %s", filename);
        file.close();
        return false;
    }

    file.close();
    return true;
}


extern "C" JNIEXPORT void JNICALL
Java_com_example_fiseye_MainActivity_triggerPhotoSelectionFromCpp(
        JNIEnv* env,
        jobject thiz) {
    LOGD("triggerPhotoSelectionFromCpp called");

    jclass mainActivityClass = env->GetObjectClass(thiz);
    if (mainActivityClass == nullptr) {
        LOGE("Failed to get MainActivity class");
        return;
    }

    jmethodID midRequestPickImage = env->GetMethodID(mainActivityClass,
                                                     "requestPickImageViaKotlin",
                                                     "()V");
    if (midRequestPickImage == nullptr) {
        LOGE("Failed to get methodID for requestPickImageViaKotlin");
        env->DeleteLocalRef(mainActivityClass);
        return;
    }

    LOGD("Calling requestPickImageViaKotlin from C++");
    env->CallVoidMethod(thiz, midRequestPickImage);

    env->DeleteLocalRef(mainActivityClass);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_fiseye_MainActivity_processImageFileInCpp(
        JNIEnv* env,
        jobject /* thiz */,
        jstring inputFilePath) {

    const char *inputPathChars = env->GetStringUTFChars(inputFilePath, nullptr);
    if (inputPathChars == nullptr) {
        LOGE("Input file path is null in C++");
        return nullptr;
    }
    std::string inputPathStr(inputPathChars);
    LOGD("C++ received input file path: %s", inputPathStr.c_str());

    // Construct processed path to be a .bmp file
    std::string processedFilePathString = inputPathStr;
    size_t dot_pos = processedFilePathString.rfind('.');
    if (dot_pos != std::string::npos) {
        processedFilePathString.replace(dot_pos, std::string::npos, "_processed.bmp");
    } else {
        processedFilePathString += "_processed.bmp";
    }
    LOGD("C++ constructed processed file path for BMP: %s", processedFilePathString.c_str());

    int width, height, channels;
    unsigned char *image_data = stbi_load(inputPathStr.c_str(), &width, &height, &channels, 0);
    //mmmmmmmaaaaaaallooooooocccccccccccccc
    if (image_data == nullptr) {
        LOGE("Failed to load image using stb_image: %s. Reason: %s", inputPathStr.c_str(), stbi_failure_reason());
        env->ReleaseStringUTFChars(inputFilePath, inputPathChars);
        return nullptr;
    }
    LOGD("Loaded image: %d x %d, channels: %d", width, height, channels);

    if (channels < 1) { // Should not happen with stb_image
        LOGE("Image has unsupported channel count: %d", channels);
        stbi_image_free(image_data);
        env->ReleaseStringUTFChars(inputFilePath, inputPathChars);
        return nullptr;
    }
    
    // Check mem lim
    const long MAX_PIXELS = (10000 * 10000); // Max 10k x 10k image
    if (static_cast<long>(width) * height > MAX_PIXELS) {
        LOGE("Image dimensions too large: %d x %d", width, height);
        stbi_image_free(image_data);
        env->ReleaseStringUTFChars(inputFilePath, inputPathChars);
        return nullptr;
    }

    std::vector<uint8_t> processed_pixels(static_cast<size_t>(width) * height * channels);

    // Fisheye
    {
        float centerX = width / 2.0f;
        float centerY = height / 2.0f;
        float radius = std::min(centerX, centerY);
        float strength = 0.5f; // 0=none, 1=strong. Adjust as needed.

        for (int y_coord = 0; y_coord < height; ++y_coord) {
            for (int x_coord = 0; x_coord < width; ++x_coord) {
                float dx = (static_cast<float>(x_coord) - centerX) / radius;
                float dy = (static_cast<float>(y_coord) - centerY) / radius;
                float r_squared = dx * dx + dy * dy;

                size_t dstIdxBase = (static_cast<size_t>(y_coord) * width + x_coord) * channels;

                if (r_squared < 1.0f) {
                    float r_val = sqrtf(r_squared);
                    float nr = powf(r_val, 1.0f - strength);
                    float theta = atan2f(dy, dx);

                    float srcX_float = centerX + nr * radius * cosf(theta);
                    float srcY_float = centerY + nr * radius * sinf(theta);

                    int srcX = static_cast<int>(roundf(srcX_float));
                    int srcY = static_cast<int>(roundf(srcY_float));

                    if (srcX >= 0 && srcX < width && srcY >= 0 && srcY < height) {
                        size_t srcIdxBase = (static_cast<size_t>(srcY) * width + srcX) * channels;
                        for(int c = 0; c < channels; ++c) {
                           if (dstIdxBase + c < processed_pixels.size() && srcIdxBase + c < (static_cast<size_t>(width) * height * channels) ) {
                                processed_pixels[dstIdxBase + c] = image_data[srcIdxBase + c];
                           }
                        }
                    } else { // Pixel is outside bounds after transform, fill with black or edge color
                         for(int c = 0; c < channels; ++c) {
                            if (dstIdxBase + c < processed_pixels.size()) {
                                processed_pixels[dstIdxBase + c] = 0; // Black
                            }
                        }
                    }
                } else { // Outside the fisheye circle, copy original pixel
                    size_t srcIdxBase = (static_cast<size_t>(y_coord) * width + x_coord) * channels;
                     for(int c = 0; c < channels; ++c) {
                        if (dstIdxBase + c < processed_pixels.size() && srcIdxBase + c < (static_cast<size_t>(width) * height * channels)) {
                             processed_pixels[dstIdxBase + c] = image_data[srcIdxBase + c];
                        }
                    }
                }
            }
        }
    }

    stbi_image_free(image_data); // Freeeeeeeeeeeeeeeeee


    if (!save_pixels_as_bmp(processedFilePathString.c_str(), processed_pixels.data(), width, height, channels)) {
        LOGE("Failed to save processed image as BMP: %s", processedFilePathString.c_str());
        env->ReleaseStringUTFChars(inputFilePath, inputPathChars);
        return nullptr; // Return null if BMP saving fails
    }

    env->ReleaseStringUTFChars(inputFilePath, inputPathChars);
    return env->NewStringUTF(processedFilePathString.c_str());
}

