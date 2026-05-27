/**
 * @file gateway_sdk.cpp
 * @brief IoT 网关 SDK 实现
 */

#include "gateway_sdk.h"
#include "logger.h"
#include <memory>
#include <cstdio>

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

    sensor_.reset(new SensorReader(cfg_.pluginDir));

    if (!cfg_.pluginPath.empty()) {
        sensor_->setPluginPath(cfg_.pluginPath);
    }

    if (!cfg_.pluginConfig.empty()) {
        sensor_->setPluginConfig(cfg_.pluginConfig);
    } else if (!cfg_.i2cDev.empty()) {
        char addrBuf[64];
        snprintf(addrBuf, sizeof(addrBuf), "i2c=%s;sht30=0x%02x;bh1750=0x%02x",
                 cfg_.i2cDev.c_str(), cfg_.sht30Addr, cfg_.bh1750Addr);
        sensor_->setPluginConfig(addrBuf);
    }

    if (!sensor_->init()) {
        LOG_E("SDK", "sensor init failed");
        return false;
    }
    if (sensor_->isSimulated()) {
        LOG_W("SDK", "running in SIMULATED sensor mode - check plugin/I2C");
    }

    LOG_I("SDK", "active plugin: %s", sensor_->getPluginName().c_str());

    dataMgr_.reset(new DataManager());

    edge_.reset(new EdgeCompute());
    EdgeCompute::Config edgeCfg;
    edgeCfg.enabled = true;
    edgeCfg.filterWindowSize = 5;
    edgeCfg.statsWindowSec = 60;
    edgeCfg.ringBufferSize = 3600;
    edge_->init(edgeCfg, cfg_.collectIntervalMs);

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
    if (!dataMgr_) {
        LOG_E("SDK", "not initialized, call init() first");
        return false;
    }

    if (!dataMgr_->start(sensor_.get(), cfg_.collectIntervalMs)) {
        LOG_E("SDK", "data manager start failed");
        return false;
    }

    dataMgr_->registerCallback([this](const SensorData &raw) {
        if (edge_) {
            edge_->processData(raw);
        }
    });

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

bool GatewaySDK::hotSwapPlugin(const std::string &newPath, const std::string &config)
{
    if (!sensor_) return false;
    return sensor_->hotSwapPlugin(newPath, config);
}

std::string GatewaySDK::getPluginName() const
{
    if (!sensor_) return "none";
    return sensor_->getPluginName();
}

std::vector<PluginInfo> GatewaySDK::listPlugins()
{
    if (!sensor_) return {};
    return sensor_->listPlugins();
}

EdgeSensorData GatewaySDK::getLatestEdgeData()
{
    if (edge_) return edge_->getLatestProcessed();
    return EdgeSensorData();
}

std::vector<AlertEvent> GatewaySDK::getActiveAlerts()
{
    if (edge_) return edge_->getActiveAlerts();
    return {};
}

void GatewaySDK::onAlert(AlertCallback cb)
{
    if (edge_) edge_->registerAlertCallback(cb);
}

ResourceUsage GatewaySDK::getResourceUsage()
{
    if (edge_) return edge_->getResourceUsage();
    return ResourceUsage();
}

}
