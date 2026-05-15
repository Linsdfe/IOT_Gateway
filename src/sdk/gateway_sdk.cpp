/**
 * @file gateway_sdk.cpp
 * @brief IoT 网关 SDK 实现
 */

#include "gateway_sdk.h"
#include "logger.h"
#include <memory>

namespace iot {

GatewaySDK::GatewaySDK()
    : running_(false)
{
}

GatewaySDK::~GatewaySDK()
{
    stop();
}

bool GatewaySDK::init(const GatewayConfig &cfg)
{
    cfg_ = cfg;

    sensor_.reset(new SensorReader(cfg_.i2cDev, cfg_.sht30Addr, cfg_.bh1750Addr));
    if (!sensor_->init()) {
        LOG_E("SDK", "sensor init failed");
        return false;
    }
    if (sensor_->isSimulated()) {
        LOG_W("SDK", "running in SIMULATED sensor mode - check I2C connections");
    }

    dataMgr_.reset(new DataManager());

    if (cfg_.enableMqtt) {
        mqtt_.reset(new MqttPublisher());
        if (!mqtt_->init(cfg_.mqtt)) {
            LOG_W("SDK", "mqtt init failed");
        }
    }

    if (cfg_.enableDisplay) {
#if defined(USE_LVGL)
        display_.reset(new LvglDisplay());
#else
        display_.reset(new DisplayManager());
#endif
        if (!display_->init(cfg_.display)) {
            LOG_W("SDK", "display init failed");
        }
    }

    LOG_I("SDK", "init OK");
    return true;
}

bool GatewaySDK::start()
{
    if (!dataMgr_->start(sensor_.get(), cfg_.collectIntervalMs)) {
        LOG_E("SDK", "data manager start failed");
        return false;
    }

    if (mqtt_ && cfg_.enableMqtt) {
        mqtt_->start(dataMgr_.get());
    }

    if (display_ && cfg_.enableDisplay) {
        display_->start(dataMgr_.get());
    }

    running_ = true;
    LOG_I("SDK", "started");
    return true;
}

void GatewaySDK::stop()
{
    running_ = false;
    if (display_) display_->stop();
    if (mqtt_) mqtt_->stop();
    if (dataMgr_) dataMgr_->stop();
    LOG_I("SDK", "stopped");
}

SensorData GatewaySDK::getLatestData()
{
    if (dataMgr_) return dataMgr_->getLatestData();
    return {0, 0, 0, false};
}

void GatewaySDK::onData(SensorDataCallback cb)
{
    if (dataMgr_) dataMgr_->registerCallback(cb);
}

bool GatewaySDK::readSensors(float &temp, float &humi, float &light)
{
    if (!sensor_) return false;
    SensorData data;
    if (!sensor_->readAll(data)) return false;
    temp = data.temperature;
    humi = data.humidity;
    light = data.light;
    return true;
}

bool GatewaySDK::sendToCloud(const std::string &json)
{
    if (!mqtt_) return false;
    return mqtt_->publishData(cfg_.mqtt.topic, json);
}

bool GatewaySDK::isRunning() const
{
    return running_;
}

}
