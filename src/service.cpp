#include <service.h>
#include <HttpClient.h>

Logger logger("app.service");

HttpClient http_client;

/**
* Constructor.
*/
DataService::DataService(String host, int port, String scheme)
{
    // PLATFORM_IDs defined here: https://github.com/particle-iot/device-os/blob/develop/hal/shared/platforms.h
    #if PLATFORM_ID == PLATFORM_PHOTON || PLATFORM_ID == PLATFORM_PHOTON_PRODUCTION
        _model = "Photon";
    #endif

    #if PLATFORM_ID == PLATFORM_ELECTRON || PLATFORM_ID == PLATFORM_ELECTRON_PRODUCTION
        _model = "Electron";
    #endif

    #if PLATFORM_ID == PLATFORM_ARGON
        _model = "Argon";
    #endif

    #if PLATFORM_ID == PLATFORM_BORON
        _model = "Boron";
    #endif

    _host = host;
    _port = port;
    _scheme = scheme;
}

bool DataService::ping(){
    int cnt = 0;
    while(cnt < DS_MAX_RETRY){
        http_response_t response = _get("/health");
        if (response.status == 200){
            return true;
        }
        cnt = cnt + 1;
    }
    return false;
}

device_data_t DataService::getDeviceData(String id){
    DynamicJsonDocument doc = _getJson("/api/v1/fermentation/controllers/" + id, 256);
    return _parseDeviceData(doc);
}

device_data_t DataService::findDevice(String manufacturerId){
    String path = "/api/v1/fermentation/controllers/find?manufacturer=";
    path.concat(MANUFACTURER);
    path.concat("&model=");
    path.concat(_model);
    path.concat("&manufacturer_id=");
    path.concat(manufacturerId);
    DynamicJsonDocument doc = _getJson(path, 256);
    
    return _parseDeviceData(doc);
}

device_data_t DataService::registerDevice(String manufacturerId, double targetTemp, double calibrationDiff){
    String path = "/api/v1/fermentation/controllers/";

    const uint16_t docSize = 192;
    DynamicJsonDocument jData(docSize);
    jData["manufacturerId"] =  manufacturerId.c_str();
    jData["manufacturer"] = MANUFACTURER.c_str();
    jData["model"] = _model.c_str();
    jData["targetTemperature"] = String(targetTemp, 2).toFloat();
    jData["calibrationDifferential"] = String(calibrationDiff, 2).toFloat();

    device_data_t res = {true};
    int cnt = 0;
    while(cnt < DS_MAX_RETRY){
        http_response_t response = _post(path, jData, docSize);
        if (response.status < 300){
            return _parseDeviceData(_respToJson(response, 256));
        }
        cnt = cnt + 1;
    }
    return res;
}

bool DataService::sendStats(String id, device_stats_t stats[], uint8_t size){
    String path = "/api/v1/fermentation/controllers/" + id + "/stats";

    const uint16_t docSize = 384;
    DynamicJsonDocument jData(docSize);
    for(uint8_t i = 0; i < size; i++) {
        JsonObject jItem = jData.createNestedObject();
        const device_stats_t s = stats[i];
        jItem["t"] = s.temperature;
        jItem["ts"] = s.timestamp;
    }

    int cnt = 0;
    while(cnt < DS_MAX_RETRY){
        http_response_t response = _post(path, jData, docSize);
        if (response.status < 300) {
            return true;
        }
        cnt = cnt + 1;
    }
    return false;
}

bool DataService::updateTargetTemp(String id, double targetTemp){
    return updateDeviceValue(id, "targetTemperature", String(targetTemp, 2));
}

bool DataService::updateCalibrationDiff(String id, double calibrationDiff){
    return updateDeviceValue(id, "calibrationDifferential", String(calibrationDiff, 2));
}

bool DataService::updatePrecision(String id, double precision) {
    return updateDeviceValue(id, "targetTemperaturePrecision", String(precision, 2));
}

bool DataService::updateHeatingDifferential(String id, double heatingDifferential) {
    return updateDeviceValue(id, "temperatureVariable", String(heatingDifferential, 2));
}

bool DataService::updateCoolingDifferential(String id, double coolingDifferential) {
    return updateDeviceValue(id, "temperatureVariable", String(coolingDifferential, 2));
}

bool DataService::updateProgramState(String id, bool programOn) {
    return updateDeviceValue(id, "programOn", programOn ? "true": "false");
}

bool DataService::updateDeviceValue(String id, String key, String value){
    String path = "/api/v1/fermentation/controllers/" + id;

    DynamicJsonDocument jData(128);
    jData[key.c_str()] = value.c_str();

    int cnt = 0;
    while(cnt < DS_MAX_RETRY){
        http_response_t response = _patch(path, jData);
        if (response.status < 300){
            return true;
        }
        cnt = cnt + 1;
    }
    return false;
}

DynamicJsonDocument DataService::_getJson(String path, uint16_t docSize) {
    return _respToJson(_get(path), docSize);
}

http_response_t DataService::_get(String path) {
    return _get(_buildRequest(path));
}

http_response_t DataService::_get(http_request_t request){
    http_response_t response;

    logger.trace("GET %s://%s:%d%s", _scheme.c_str(), request.hostname.c_str(), request.port, request.path.c_str());

    http_header_t headers[] = {
        { "Accept" , "application/json" },
        { NULL, NULL } // NOTE: Always terminate headers will NULL
    };
    http_client.get(request, response, headers);

    logger.trace("Response status: %d", response.status);
    logger.trace("Response Body: %s", response.body.c_str());

    return response;
}

http_response_t DataService::_patch(String path, DynamicJsonDocument jDoc, const uint16_t docSize){
    return _post(_buildRequest(path, jDoc, docSize));
}

http_response_t DataService::_patch(String path, String data){
    return _post(_buildRequest(path, data));
}

http_response_t DataService::_patch(http_request_t request){
    http_response_t response;

    logger.trace("PATCH %s://%s:%d%s", _scheme.c_str(), request.hostname.c_str(), request.port, request.path.c_str());
    logger.trace("Data: %s", request.body.c_str());

    http_header_t headers[] = {
        { "Content-Type", "application/json" },
        { "Accept" , "application/json" },
        { NULL, NULL } // NOTE: Always terminate headers will NULL
    };
    http_client.patch(request, response, headers);

    logger.trace("Response status: %d", response.status);
    logger.trace("Response Body: %s", response.body.c_str());
    
    return response;
}

http_response_t DataService::_post(String path, DynamicJsonDocument jDoc, const uint16_t docSize){
    return _post(_buildRequest(path, jDoc, docSize));
}
http_response_t DataService::_post(String path, String data){
    return _post(_buildRequest(path, data));
}

http_response_t DataService::_post(http_request_t request){
    http_response_t response;

    logger.trace("POST %s://%s:%d%s", _scheme.c_str(), request.hostname.c_str(), request.port, request.path.c_str());
    logger.trace("Data: %s", request.body.c_str());

    http_header_t headers[] = {
        { "Content-Type", "application/json" },
        { "Accept" , "application/json" },
        { NULL, NULL } // NOTE: Always terminate headers will NULL
    };
    http_client.post(request, response, headers);

    logger.trace("Response status: %d", response.status);
    logger.trace("Response Body: %s", response.body.c_str());
    
    return response;
}

http_request_t DataService::_buildRequest(String path) {
    http_request_t request;
    request.hostname = _host;
    request.port = _port;
    request.path = path;
    return request;
}

http_request_t DataService::_buildRequest(String path, String data) {
    http_request_t request = _buildRequest(path);
    request.body = data;
    return request;
}

http_request_t DataService::_buildRequest(String path, DynamicJsonDocument jDoc, const uint16_t docSize) {
    char data[1024];
    serializeJson(jDoc, data);
    return _buildRequest(path, data);
}

DynamicJsonDocument DataService::_respToJson(http_response_t response, const uint16_t docSize) {
    DynamicJsonDocument doc(docSize);
    if (response.status == 200){
        DeserializationError error = deserializeJson(doc, response.body.c_str());
        if (error) {
            logger.error("deserializeJson() failed: ");
            // DO SOMETHING TO THROW AN ERROR 
        }
    }

    return doc;
}

device_data_t DataService::_parseDeviceData(DynamicJsonDocument jDoc) {
    device_data_t res = {true};
    if (!jDoc.isNull()) {
        res.isNull = false;
        JsonObject deviceDetails = jDoc.as<JsonObject>();
        res.id = String(deviceDetails["id"].as<const char*>());
        res.manufacturerId = String(deviceDetails["manufacturerId"].as<const char*>());
        res.targetTemp = deviceDetails["targetTemperature"].as<float>();
        res.calibrationDiff = deviceDetails["calibrationDifferential"].as<float>();
        res.coolingDifferential = deviceDetails["coolingDifferential"].as<float>();
        res.heatingDifferential = deviceDetails["heatingDifferential"].as<float>();
        res.tempPrecision = deviceDetails["temperaturePrecision"].as<float>();
        res.programOn = deviceDetails["programOn"].as<bool>();
    }
    return res;
}
