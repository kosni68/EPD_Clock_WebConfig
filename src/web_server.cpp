#include "web_server.h"
#include "config.h"
#include "config_manager.h"
#include "utils.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

static AsyncWebServer server(80);

static void handleGetConfig(AsyncWebServerRequest *request);
static void handlePostConfig(AsyncWebServerRequest *request, const String &body);

void startWebServer()
{
    Serial.println("[WEB] Initialisation du serveur HTTP...");

    if (!LittleFS.begin(true))
    {
        Serial.println("[WEB][ERREUR] Échec du montage LittleFS !");
        while (true)
            delay(1000);
    }

    if (!connectWiFiShort(8000))
    {
        Serial.println("[WEB][WARN] Échec de connexion Wi-Fi. Activation du mode point d’accès...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("EPD_Clock");
        Serial.print("[WEB] Point d’accès actif : ");
        Serial.println(WiFi.softAPIP());
    }
    else
    {
        Serial.print("[WEB] Connecté au Wi-Fi : ");
        Serial.println(WiFi.localIP());
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /index.html");
        if (LittleFS.exists("/index.html")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Content-Type", "text/html; charset=utf-8");
            request->send(response);
        } else {
            request->send(200, "text/html; charset=utf-8",
                          "<!doctype html><html><body><h2>EPD Clock</h2>"
                          "<p><a href=\"/config.html\">Configuration</a></p></body></html>");
        } });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /style.css");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/style.css", "text/css");
        response->addHeader("Content-Type", "text/css; charset=utf-8");
        request->send(response); });

    server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            Serial.println("[WEB][AUTH] Auth requise sur /config.html");
            return request->requestAuthentication();
        }

        Serial.println("[WEB] GET /config.html (auth OK)");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/config.html", "text/html");
        response->addHeader("Content-Type", "text/html; charset=utf-8");
        request->send(response); });

    server.on("/script_config.js", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            Serial.println("[WEB][AUTH] Auth requise sur /script_config.js");
            return request->requestAuthentication();
        }
        Serial.println("[WEB] GET /script_config.js (auth OK)");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/script_config.js", "application/javascript");
        response->addHeader("Content-Type", "application/javascript; charset=utf-8");
        request->send(response); });

    server.on("/ping", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        String page = request->arg("page");
        interactiveLastTouchMs.store(millis());
        Serial.printf("[WEB] POST /ping (%s)\n", page.c_str());
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}"); });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /api/config");
        handleGetConfig(request); });

    server.on("/api/config", HTTP_POST,
              [](AsyncWebServerRequest *request) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
                  const char *adminUser = ConfigManager::instance().getAdminUser();
                  const char *adminPass = ConfigManager::instance().getAdminPass();
                  if (!request->authenticate(adminUser, adminPass))
                  {
                      Serial.println("[WEB][AUTH] /api/config POST non autorisé");
                      request->requestAuthentication();
                      return;
                  }

                  if (index == 0)
                  {
                      if (total > 4096)
                      {
                          Serial.printf("[WEB] Payload trop gros (%u)\n", (unsigned)total);
                          request->send(413, "application/json; charset=utf-8",
                                        "{\"ok\":false,\"err\":\"payload too large\"}");
                          return;
                      }
                      request->_tempObject = new String();
                      ((String *)request->_tempObject)->reserve(total);
                      Serial.printf("[WEB] Début réception body JSON (%u octets)\n", (unsigned)total);
                  }

                  String *body = reinterpret_cast<String *>(request->_tempObject);
                  if (body)
                  {
                      body->concat(String((const char *)data).substring(0, len));
                  }

                  if (index + len == total)
                  {
                      Serial.printf("[WEB] Corps JSON complet reçu (%u octets)\n", (unsigned)total);
                      if (body)
                      {
                          handlePostConfig(request, *body);
                          delete body;
                          request->_tempObject = nullptr;
                      }
                  }
              });

    server.begin();
    Serial.println("[WEB] Serveur Web démarré.");
}

static void handleGetConfig(AsyncWebServerRequest *request)
{
    const char *adminUser = ConfigManager::instance().getAdminUser();
    const char *adminPass = ConfigManager::instance().getAdminPass();
    if (!request->authenticate(adminUser, adminPass))
    {
        Serial.println("[WEB][AUTH] /api/config GET non autorisé");
        return request->requestAuthentication();
    }

    Serial.println("[WEB] GET /api/config (auth OK)");
    String json = ConfigManager::instance().toJsonString();
    request->send(200, "application/json; charset=utf-8", json);
}

static void handlePostConfig(AsyncWebServerRequest *request, const String &body)
{
    Serial.println("[WEB] POST /api/config reçu");

    const char *adminUser = ConfigManager::instance().getAdminUser();
    const char *adminPass = ConfigManager::instance().getAdminPass();
    if (!request->authenticate(adminUser, adminPass))
    {
        Serial.println("[WEB][AUTH] /api/config POST non autorisé (2nd check)");
        return request->requestAuthentication();
    }

    if (body.isEmpty())
    {
        Serial.println("[WEB][ERR] Corps JSON vide !");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"empty body\"}");
        return;
    }

    bool okUpdate = ConfigManager::instance().updateFromJson(body);
    if (okUpdate)
    {
        Serial.println("[WEB] Configuration mise à jour (sauvegarde différée).");
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
    }
    else
    {
        Serial.println("[WEB][ERR] Échec de la mise à jour JSON !");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false}");
    }
}
