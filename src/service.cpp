#include <service.h>
#include <HttpClient.h>

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
    DynamicJsonDocument doc = _getJson("/api/v1/fermentation/controllers/" + id);
    device_data_t res;

    return res;
}

device_data_t DataService::findDevice(String manufacturerId){
    String path = "/api/v1/fermentation/controllers/find?manufacturer=";
    path.concat(MANUFACTURER);
    path.concat("&model=");
    path.concat(_model);
    path.concat("&manufacturer_id=");
    path.concat(manufacturerId);
    DynamicJsonDocument doc = _getJson(path);
    
    device_data_t res;
    return res;
}

device_data_t DataService::registerDevice(String manufacturerId, double targetTemp, double calibrationDiff){
    String path = "/api/v1/fermentation/controllers/";

    DynamicJsonDocument jData(1024);
    jData["manufacturerId"] = manufacturerId.c_str();
    jData["manufacturer"] = MANUFACTURER.c_str();
    jData["model"] = _model.c_str();
    jData["targetTemperature"] = String(targetTemp, 2).c_str();
    jData["calibrationDifferential"] = String(calibrationDiff, 2).c_str();

    device_data_t res = {true};
    int cnt = 0;
    while(cnt < DS_MAX_RETRY){
        http_response_t response = _post(path, jData);
        if (response.status < 300){
            return _parseDeviceData(_respToJson(response));
        }
        cnt = cnt + 1;
    }
    return res;
}

bool DataService::sendStats(String id, device_stats_t stats){
    String path = "/api/v1/fermentation/controllers/" + id + "/stats";

    DynamicJsonDocument jData(1024);
    jData["temperature"] = String(stats.currentTemp, 2).c_str();

    int cnt = 0;
    while(cnt < DS_MAX_RETRY){
        http_response_t response = _post(path, jData);
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
    return updateDeviceValue(id, "targetTemperaturePrecision", String(calibrationDiff, 2));
}

bool DataService::updateTempVariable(String id, double tempVariable) {
    return updateDeviceValue(id, "temperatureVariable", String(calibrationDiff, 2));
}

bool DataService::updateDeviceValue(String id, String key, String value){
    String path = "/api/v1/fermentation/controllers/" + id;

    DynamicJsonDocument jData(1024);
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

DynamicJsonDocument DataService::_getJson(String path) {
    return _respToJson(_get(path));
}

http_response_t DataService::_get(String path) {
    return _get(_buildRequest(path));
}

http_response_t DataService::_get(http_request_t request){
    http_response_t response;

    Serial.printlnf("DataService>\tGET %s://%s:%d%s", _scheme.c_str(), request.hostname.c_str(), request.port, request.path.c_str());

    http_header_t headers[] = {
        { "Accept" , "application/json" },
        { NULL, NULL } // NOTE: Always terminate headers will NULL
    };
    http_client.get(request, response, headers);

    Serial.printlnf("DataService>\tResponse status: %d", response.status);
    Serial.printlnf("DataService>\tResponse Body: %s", response.body.c_str());

    return response;
}

http_response_t DataService::_patch(String path, DynamicJsonDocument jDoc, int docSize){
    return _post(_buildRequest(path, jDoc, docSize));
}

http_response_t DataService::_patch(String path, String data){
    return _post(_buildRequest(path, data));
}

http_response_t DataService::_patch(http_request_t request){
    http_response_t response;

    Serial.printlnf("DataService>\tPATCH %s://%s:%d%s", _scheme.c_str(), request.hostname.c_str(), request.port, request.path.c_str());
    Serial.printlnf("DataService>\tData: %s", request.body.c_str());

    http_header_t headers[] = {
        { "Content-Type", "application/json" },
        { "Accept" , "application/json" },
        { NULL, NULL } // NOTE: Always terminate headers will NULL
    };
    http_client.patch(request, response, headers);

    Serial.printlnf("DataService>\tResponse status: %d", response.status);
    Serial.printlnf("DataService>\tResponse Body: %s", response.body.c_str());
    
    return response;
}

http_response_t DataService::_post(String path, DynamicJsonDocument jDoc, int docSize){
    return _post(_buildRequest(path, jDoc, docSize));
}

http_response_t DataService::_post(String path, String data){
    return _post(_buildRequest(path, data));
}

http_response_t DataService::_post(http_request_t request){
    http_response_t response;

    Serial.printlnf("DataService>\tPOST %s://%s:%d%s", _scheme.c_str(), request.hostname.c_str(), request.port, request.path.c_str());
    Serial.printlnf("DataService>\tData: %s", request.body.c_str());

    http_header_t headers[] = {
        { "Content-Type", "application/json" },
        { "Accept" , "application/json" },
        { NULL, NULL } // NOTE: Always terminate headers will NULL
    };
    http_client.post(request, response, headers);

    Serial.printlnf("DataService>\tResponse status: %d", response.status);
    Serial.printlnf("DataService>\tResponse Body: %s", response.body.c_str());
    
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

http_request_t DataService::_buildRequest(String path, DynamicJsonDocument jDoc, int docSize) {
    char data[1024];
    serializeJson(jDoc, data);
    return _buildRequest(path, data);
}

DynamicJsonDocument DataService::_respToJson(http_response_t response) {
    DynamicJsonDocument doc(1024);
    if (response.status == 200){
        DeserializationError error = deserializeJson(doc, response.body.c_str());
        if (error) {
            Serial.print(F("deserializeJson() failed: "));
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
        res.targetTemp = deviceDetails["targetTemp"].as<int>();
    }
    return res;
}