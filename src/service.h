#ifndef __DATA_SERVICE_H__
#define __DATA_SERVICE_H__

#include <ArduinoJson.h>
#include <HttpClient.h>

const String MANUFACTURER = "Particle";
const int DS_MAX_RETRY = 2;

typedef struct {
    bool isNull;
    String id;
    String manufacturerId;
    double targetTemp;
    double calibrationDiff;
    double tempVariable;
    double precision;
} device_data_t;

typedef struct {
    double currentTemp;
} device_stats_t;

class DataService {
public:
    DataService(String host, int port, String scheme="http");
    bool ping();
    device_data_t findDevice(String manufacturerId);
    device_data_t getDeviceData(String id);
    device_data_t registerDevice(String manufacturerId, double targetTemp, double calibrationDiff);
    bool sendStats(String id, device_stats_t stats);
    bool updateTargetTemp(String id, double targetTemp);
    bool updateCalibrationDiff(String id, double calibrationDiff);
    bool updatePrecision(String id, double precision);
    bool updateTempVariable(String id, double tempVariable);
    bool updateDeviceValue(String id, String key, String value);

private:
    String _host;
    int _port;
    String _scheme;
    String _model;

    http_request_t _buildRequest(String path);
    http_request_t _buildRequest(String path, String data);
    http_request_t _buildRequest(String path, DynamicJsonDocument jDoc, int docSize=1024);
    DynamicJsonDocument _getJson(String path);
    http_response_t _get(String path);
    http_response_t _get(http_request_t request);
    http_response_t _patch(String path, String data);
    http_response_t _patch(String path, DynamicJsonDocument jDoc, int docSize=1024);
    http_response_t _patch(http_request_t request);
    http_response_t _post(String path, String data);
    http_response_t _post(String path, DynamicJsonDocument jDoc, int docSize=1024);
    http_response_t _post(http_request_t request);
    DynamicJsonDocument _respToJson(http_response_t response);
    device_data_t _parseDeviceData(DynamicJsonDocument jDoc);
};

#endif /* __DATA_SERVICE_H__ */