#include "gateway_sdk.h"
#include <cstdio>
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
        fprintf(stderr, "[GatewaySDK] sensor init failed\n");
        return false;
    }

    dataMgr_.reset(new DataManager());

    if (cfg_.enableMqtt) {
        mqtt_.reset(new MqttPublisher());
        if (!mqtt_->init(cfg_.mqtt)) {
            fprintf(stderr, "[GatewaySDK] mqtt init failed\n");
        }
    }

    if (cfg_.enableDisplay) {
        display_.reset(new DisplayManager());
        if (!display_->init(cfg_.display)) {
            fprintf(stderr, "[GatewaySDK] display init failed\n");
        }
    }

    printf("[GatewaySDK] init OK\n");
    return true;
}

bool GatewaySDK::start()
{
    if (!dataMgr_->start(sensor_.get(), cfg_.collectIntervalMs)) {
        fprintf(stderr, "[GatewaySDK] data manager start failed\n");
        return false;
    }

    if (mqtt_ && cfg_.enableMqtt) {
        mqtt_->start(dataMgr_.get());
    }

    if (display_ && cfg_.enableDisplay) {
        display_->start(dataMgr_.get());
    }

    running_ = true;
    printf("[GatewaySDK] started\n");
    return true;
}

void GatewaySDK::stop()
{
    running_ = false;
    if (display_) display_->stop();
    if (mqtt_) mqtt_->stop();
    if (dataMgr_) dataMgr_->stop();
    printf("[GatewaySDK] stopped\n");
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
