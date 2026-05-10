#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "sensor_reader.h"
#include <thread>
#include <atomic>
#include <string>

class DisplayManager {
public:
    struct Config {
        std::string fbDevice;
        int width;
        int height;
        bool enabled;
    };

    DisplayManager();
    ~DisplayManager();

    bool init(const Config &cfg);
    bool start(class DataManager *mgr);
    void stop();

private:
    void displayLoop();
    void drawText(int x, int y, const char *text, uint32_t color);
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void updateDisplay(const SensorData &data);

    Config cfg_;
    DataManager *mgr_;
    std::thread displayThread_;
    std::atomic<bool> running_;

    int fbFd_;
    void *fbMem_;
    int fbWidth_;
    int fbHeight_;
    int fbBpp_;
    int fbLineLen_;
};

#endif
