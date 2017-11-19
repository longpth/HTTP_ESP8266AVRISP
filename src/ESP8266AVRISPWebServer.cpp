/*
AVR ISP Programming over HTTP for ESP8266
Copyright (c) Long Pham <it.farmer.vn@gmail.com>

Original version:
	AVR In-System Programming over WiFi for ESP8266
	Copyright (c) Kiril Zyapkov <kiril@robotev.com>

    ArduinoISP version 04m3
    Copyright (c) 2008-2011 Randall Bohn
    If you require a license, see
        http://www.opensource.org/licenses/bsd-license.php
*/
#include "ESP8266AVRISPWebServer.h"
#include <Arduino.h>
#include <SPI.h>
#include <pgmspace.h>
#include <ESP8266WiFi.h>
#include <map>

#include "httpcommand.h"

extern "C" {
    #include "user_interface.h"
    #include "mem.h"
}

#define malloc      os_malloc
#define free        os_free

// #define AVRISP_DEBUG(fmt, ...)     os_printf("[AVRP] " fmt "\r\n", ##__VA_ARGS__ )
#define AVRISP_DEBUG(...)

#define AVRISP_HWVER 2
#define AVRISP_SWMAJ 1
#define AVRISP_SWMIN 18
#define AVRISP_PTIME 10

#define EECHUNK (32)

#define beget16(addr) (*addr * 256 + *(addr+1))

#define HTTP_ESPAVRISP_DEBUG

#ifdef HTTP_ESPAVRISP_DEBUG
std::map<uint8_t, char*> cmd_debug =
{
	{ Cmnd_STK_GET_SYNC         , "Cmnd_STK_GET_SYNC       " },
	{ Cmnd_STK_GET_SIGN_ON      , "Cmnd_STK_GET_SIGN_ON    " },
	{ Cmnd_STK_RESET            , "Cmnd_STK_RESET          " },
	{ Cmnd_STK_SINGLE_CLOCK     , "Cmnd_STK_SINGLE_CLOCK   " },
	{ Cmnd_STK_STORE_PARAMETERS , "Cmnd_STK_STORE_PARAMETER" },
	{ Cmnd_STK_SET_PARAMETER    , "Cmnd_STK_SET_PARAMETER  " },
	{ Cmnd_STK_GET_PARAMETER    , "Cmnd_STK_GET_PARAMETER  " },
	{ Cmnd_STK_SET_DEVICE       , "Cmnd_STK_SET_DEVICE     " },
	{ Cmnd_STK_GET_DEVICE       , "Cmnd_STK_GET_DEVICE     " },
	{ Cmnd_STK_GET_STATUS       , "Cmnd_STK_GET_STATUS     " },
	{ Cmnd_STK_SET_DEVICE_EXT   , "Cmnd_STK_SET_DEVICE_EXT " },
	{ Cmnd_STK_ENTER_PROGMODE   , "Cmnd_STK_ENTER_PROGMODE " },
	{ Cmnd_STK_LEAVE_PROGMODE   , "Cmnd_STK_LEAVE_PROGMODE " },
	{ Cmnd_STK_CHIP_ERASE       , "Cmnd_STK_CHIP_ERASE     " },
	{ Cmnd_STK_CHECK_AUTOINC    , "Cmnd_STK_CHECK_AUTOINC  " },
	{ Cmnd_STK_CHECK_DEVICE     , "Cmnd_STK_CHECK_DEVICE   " },
	{ Cmnd_STK_LOAD_ADDRESS     , "Cmnd_STK_LOAD_ADDRESS   " },
	{ Cmnd_STK_UNIVERSAL        , "Cmnd_STK_UNIVERSAL      " },
	{ Cmnd_STK_PROG_FLASH       , "Cmnd_STK_PROG_FLASH     " },
	{ Cmnd_STK_PROG_DATA        , "Cmnd_STK_PROG_DATA      " },
	{ Cmnd_STK_PROG_FUSE        , "Cmnd_STK_PROG_FUSE      " },
	{ Cmnd_STK_PROG_LOCK        , "Cmnd_STK_PROG_LOCK      " },
	{ Cmnd_STK_PROG_PAGE        , "Cmnd_STK_PROG_PAGE      " },
	{ Cmnd_STK_PROG_FUSE_EXT    , "Cmnd_STK_PROG_FUSE_EXT  " },
	{ Cmnd_STK_READ_FLASH       , "Cmnd_STK_READ_FLASH     " },
	{ Cmnd_STK_READ_DATA        , "Cmnd_STK_READ_DATA      " },
	{ Cmnd_STK_READ_FUSE        , "Cmnd_STK_READ_FUSE      " },
	{ Cmnd_STK_READ_LOCK        , "Cmnd_STK_READ_LOCK      " },
	{ Cmnd_STK_READ_PAGE        , "Cmnd_STK_READ_PAGE      " },
	{ Cmnd_STK_READ_SIGN        , "Cmnd_STK_READ_SIGN      " },
	{ Cmnd_STK_READ_OSCCAL      , "Cmnd_STK_READ_OSCCAL    " },
	{ Cmnd_STK_READ_FUSE_EXT    , "Cmnd_STK_READ_FUSE_EXT  " },
	{ Cmnd_STK_READ_OSCCAL_EXT  , "Cmnd_STK_READ_OSCCAL_EXT" }
};
#endif

static char* readBytesWithTimeout2(WiFiClient& client, size_t maxLength, size_t& dataLength, int timeout_ms)
{
  char *buf = nullptr;
  dataLength = 0;
  while (dataLength < maxLength) {
    int tries = timeout_ms;
    size_t newLength;
    while (!(newLength = client.available()) && tries--) delay(1);
    if (!newLength) {
      break;
    }
    if (!buf) {
      buf = (char *) malloc(newLength + 1);
      if (!buf) {
        return nullptr;
      }
    }
    else {
      char* newBuf = (char *) realloc(buf, dataLength + newLength + 1);
      if (!newBuf) {
        free(buf);
        return nullptr;
      }
      buf = newBuf;
    }
    client.readBytes(buf + dataLength, newLength);
    dataLength += newLength;
    buf[dataLength] = '\0';
  }
  return buf;
}


ESP8266AVRISPWebServer::ESP8266AVRISPWebServer(IPAddress addr, int port, uint8_t reset_pin, uint32_t spi_freq, bool reset_state, bool reset_activehigh):
ESP8266WebServer(addr, port),
_reset_pin(reset_pin),
_reset_state(reset_state),
_spi_freq(spi_freq),
_reset_activehigh(reset_activehigh),
_state(HTTP_AVRISP_STATE_IDLE),
_currentBodyIndex(0),
_bodyLen(0)
{
	pinMode(_reset_pin, OUTPUT);
    setReset(_reset_state);
	RegisterAVRISP();
}

ESP8266AVRISPWebServer::ESP8266AVRISPWebServer(int port, uint8_t reset_pin, uint32_t spi_freq, bool reset_state, bool reset_activehigh):
ESP8266WebServer(port),
_reset_pin(reset_pin),
_reset_state(reset_state),
_spi_freq(spi_freq),
_reset_activehigh(reset_activehigh),
_state(HTTP_AVRISP_STATE_IDLE),
_currentBodyIndex(0),
_bodyLen(0)
{
	pinMode(_reset_pin, OUTPUT);
    setReset(_reset_state);
	RegisterAVRISP();
}

void ESP8266AVRISPWebServer::handleClient2()
{
	if (_currentStatus == HC_NONE) {
    WiFiClient client = _server.available();
    if (!client) {
      return;
    }

#ifdef DEBUG_ESP_HTTP_SERVER
    DEBUG_OUTPUT.println("New client");
#endif

    _currentClient = client;
    _currentStatus = HC_WAIT_READ;
    _statusChange = millis();
  }

  if (!_currentClient.connected()) {
    _currentClient = WiFiClient();
    _currentStatus = HC_NONE;
    return;
  }

  // Wait for data from client to become available
  if (_currentStatus == HC_WAIT_READ) {
    if (!_currentClient.available()) {
      if (millis() - _statusChange > HTTP_MAX_DATA_WAIT) {
        _currentClient = WiFiClient();
        _currentStatus = HC_NONE;
      }
      yield();
      return;
    }

    if (!_parseRequest2(_currentClient)) {
      _currentClient = WiFiClient();
      _currentStatus = HC_NONE;
      return;
    }

    _contentLength = CONTENT_LENGTH_NOT_SET;
    _handleRequest();

    if (!_currentClient.connected()) {
      _currentClient = WiFiClient();
      _currentStatus = HC_NONE;
      return;
    } else {
      _currentStatus = HC_WAIT_CLOSE;
      _statusChange = millis();
      return;
    }
  }

  if (_currentStatus == HC_WAIT_CLOSE) {
    if (millis() - _statusChange > HTTP_MAX_CLOSE_WAIT) {
      _currentClient = WiFiClient();
      _currentStatus = HC_NONE;
    } else {
      yield();
      return;
    }
  }
}

bool ESP8266AVRISPWebServer::_parseRequest2(WiFiClient& client) {
  // Read the first line of HTTP request
  String req = client.readStringUntil('\r');
  client.readStringUntil('\n');
#ifdef HTTP_ESPAVRISP_DEBUG
  Serial.println(req);
#endif
  //reset header value
  for (int i = 0; i < _headerKeysCount; ++i) {
    _currentHeaders[i].value =String();
   }

  // First line of HTTP request looks like "GET /path HTTP/1.1"
  // Retrieve the "/path" part by finding the spaces
  int addr_start = req.indexOf(' ');
  int addr_end = req.indexOf(' ', addr_start + 1);
  if (addr_start == -1 || addr_end == -1) {
#ifdef DEBUG_ESP_HTTP_SERVER
    DEBUG_OUTPUT.print("Invalid request: ");
    DEBUG_OUTPUT.println(req);
#endif
    return false;
  }

  String methodStr = req.substring(0, addr_start);
  String url = req.substring(addr_start + 1, addr_end);
  String searchStr = "";
  int hasSearch = url.indexOf('?');
  if (hasSearch != -1){
    searchStr = url.substring(hasSearch + 1);
    url = url.substring(0, hasSearch);
  }
  _currentUri = url;

  HTTPMethod method = HTTP_GET;
  if (methodStr == "POST") {
    method = HTTP_POST;
  } else if (methodStr == "DELETE") {
    method = HTTP_DELETE;
  } else if (methodStr == "OPTIONS") {
    method = HTTP_OPTIONS;
  } else if (methodStr == "PUT") {
    method = HTTP_PUT;
  } else if (methodStr == "PATCH") {
    method = HTTP_PATCH;
  }
  _currentMethod = method;

#ifdef DEBUG_ESP_HTTP_SERVER
  DEBUG_OUTPUT.print("method: ");
  DEBUG_OUTPUT.print(methodStr);
  DEBUG_OUTPUT.print(" url: ");
  DEBUG_OUTPUT.print(url);
  DEBUG_OUTPUT.print(" search: ");
  DEBUG_OUTPUT.println(searchStr);
#endif

  //attach handler
  RequestHandler* handler;
  for (handler = _firstHandler; handler; handler = handler->next()) {
    if (handler->canHandle(_currentMethod, _currentUri))
      break;
  }
  _currentHandler = handler;

  String formData;
  // below is needed only when POST type request
  if (method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH || method == HTTP_DELETE){
    String boundaryStr;
    String headerName;
    String headerValue;
    bool isForm = false;
    uint32_t contentLength = 0;
    //parse headers
    while(1){
      req = client.readStringUntil('\r');
      client.readStringUntil('\n');
#ifdef DEBUG_ESP_HTTP_SERVER
	  Serial.println(req);
#endif
      if (req == "") break;//no moar headers
      int headerDiv = req.indexOf(':');
      if (headerDiv == -1){
        break;
      }
      headerName = req.substring(0, headerDiv);
      headerValue = req.substring(headerDiv + 1);
      headerValue.trim();
       _collectHeader(headerName.c_str(),headerValue.c_str());

	  #ifdef DEBUG_ESP_HTTP_SERVER
	  DEBUG_OUTPUT.print("headerName: ");
	  DEBUG_OUTPUT.println(headerName);
	  DEBUG_OUTPUT.print("headerValue: ");
	  DEBUG_OUTPUT.println(headerValue);
	  #endif

      if (headerName == "Content-Type"){
        if (headerValue.startsWith("text/plain")){
          isForm = false;
        } else if (headerValue.startsWith("multipart/form-data")){
          boundaryStr = headerValue.substring(headerValue.indexOf('=')+1);
          isForm = true;
        }
      } else if (headerName == "Content-Length"){
        contentLength = headerValue.toInt();
      } else if (headerName == "Host"){
        _hostHeader = headerValue;
      }
    }

    if (!isForm){
      size_t plainLength;
      char* plainBuf = readBytesWithTimeout2(client, contentLength, plainLength, HTTP_MAX_POST_WAIT);
      if (plainLength < contentLength) {
      	free(plainBuf);
      	return false;
      }
#ifdef DEBUG_ESP_HTTP_SERVER
	  Serial.println("body data length: ");
	  Serial.println(plainLength);
	  Serial.println("method: ");
	  Serial.println(_currentMethod);
	  Serial.println("uri: ");
	  Serial.println(_currentUri);
#endif
	  for(byte i = 0; i < plainLength; ++i){
		  _body[i] = plainBuf[i];
		  Serial.print("Body data: ");
		  Serial.print(i);
		  Serial.print(" ");
		  Serial.println((int)_body[i], HEX);
	  }
	  _bodyLen = plainLength;
#ifdef DEBUG_ESP_HTTP_SERVER
      DEBUG_OUTPUT.print("Plain: ");
      DEBUG_OUTPUT.println(plainBuf);
#endif
      if (contentLength > 0) {
        if (searchStr != "") searchStr += '&';
        if(plainBuf[0] == '{' || plainBuf[0] == '[' || strstr(plainBuf, "=") == NULL){
          //plain post json or other data
          searchStr += "plain=";
          searchStr += plainBuf;
        } else {
          searchStr += plainBuf;
        }
        free(plainBuf);
      }
    }
    _parseArguments(searchStr);
    if (isForm){
      if (!_parseForm(client, boundaryStr, contentLength)) {
        return false;
      }
    }
  } else {
    String headerName;
    String headerValue;
    //parse headers
    while(1){
      req = client.readStringUntil('\r');
      client.readStringUntil('\n');
#ifdef DEBUG_ESP_HTTP_SERVER
	  Serial.println(req);
#endif
      if (req == "") break;//no moar headers
      int headerDiv = req.indexOf(':');
      if (headerDiv == -1){
        break;
      }
      headerName = req.substring(0, headerDiv);
      headerValue = req.substring(headerDiv + 2);
      _collectHeader(headerName.c_str(),headerValue.c_str());

	  #ifdef DEBUG_ESP_HTTP_SERVER
	  DEBUG_OUTPUT.print("headerName: ");
	  DEBUG_OUTPUT.println(headerName);
	  DEBUG_OUTPUT.print("headerValue: ");
	  DEBUG_OUTPUT.println(headerValue);
	  #endif

	  if (headerName == "Host"){
        _hostHeader = headerValue;
      }
    }
    _parseArguments(searchStr);
  }
  client.flush();

#ifdef DEBUG_ESP_HTTP_SERVER
  DEBUG_OUTPUT.print("Request: ");
  DEBUG_OUTPUT.println(url);
  DEBUG_OUTPUT.print(" Arguments: ");
  DEBUG_OUTPUT.println(searchStr);
#endif

  return true;
}

void ESP8266AVRISPWebServer::setSpiFrequency(uint32_t freq) {
    _spi_freq = freq;
    if (_state == HTTP_AVRISP_STATE_ACTIVE) {
        SPI.setFrequency(freq);
    }
}

void ESP8266AVRISPWebServer::RegisterAVRISP()
{
	on("/cmd", HTTP_POST, [this]{ avrisp(); });
}

void ESP8266AVRISPWebServer::setReset(bool rst) {
    _reset_state = rst;
    digitalWrite(_reset_pin, _resetLevel(_reset_state));
}

HTTPAVRISPState_t ESP8266AVRISPWebServer::update() {
    switch (_state) {
        case HTTP_AVRISP_STATE_IDLE: {
            if (_server.hasClient()) {
                _client = _server.available();
                _client.setNoDelay(true);
                ip_addr_t lip;
                lip.addr = _client.remoteIP();
                AVRISP_DEBUG("client connect %d.%d.%d.%d:%d", IP2STR(&lip), _client.remotePort());
                _client.setTimeout(100); // for getch()
                _state = HTTP_AVRISP_STATE_PENDING;
                _reject_incoming();
            }
            break;
        }
        case HTTP_AVRISP_STATE_PENDING:
        case HTTP_AVRISP_STATE_ACTIVE: {
            // handle disconnect
            if (!_client.connected()) {
                _client.stop();
                AVRISP_DEBUG("client disconnect");
                if (pmode) {
                    SPI.end();
                    pmode = 0;
                }
                setReset(_reset_state);
                _state = HTTP_AVRISP_STATE_IDLE;
            } else {
                _reject_incoming();
            }
            break;
        }
    }
    return _state;
}

HTTPAVRISPState_t ESP8266AVRISPWebServer::serve() {
    switch (update()) {
        case HTTP_AVRISP_STATE_IDLE:
            // should not be called when idle, error?
            break;
        case HTTP_AVRISP_STATE_PENDING: {
            _state = HTTP_AVRISP_STATE_ACTIVE;
        // fallthrough
        }
        case HTTP_AVRISP_STATE_ACTIVE: {
            while (_client.available()) {
                avrisp();
            }
            return update();
        }
    }
    return _state;
}

inline void ESP8266AVRISPWebServer::_reject_incoming(void) {
    while (_server.hasClient()) _server.available().stop();
}

uint8_t ESP8266AVRISPWebServer::getch() {
#if 0
    while (!_client.available()) yield();
    uint8_t b = (uint8_t)_client.read();
#endif
    uint8_t b = _body[_currentBodyIndex++];
    // AVRISP_DEBUG("< %02x", b);
    return b;
}

void ESP8266AVRISPWebServer::fill(int n) {
    // AVRISP_DEBUG("fill(%u)", n);
    for (int x = 0; x < n; x++) {
        buff[x] = getch();
    }
}

uint8_t ESP8266AVRISPWebServer::spi_transaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint8_t n;
    SPI.transfer(a);
    n = SPI.transfer(b);
    n = SPI.transfer(c);
    return SPI.transfer(d);
}

void ESP8266AVRISPWebServer::empty_reply() {
	char resp[2];
    if (Sync_CRC_EOP == getch()) {
    	resp[0] = Resp_STK_INSYNC;
    	resp[1] = Resp_STK_OK;
        //_client.print((char)Resp_STK_INSYNC);
        //_client.print((char)Resp_STK_OK);
    	send_P(200, "text/plain", (const char *)resp, 2);
    } else {
        error++;
    	resp[0] = Resp_STK_NOSYNC;
    	resp[1] = Resp_STK_OK;
        //_client.print((char)Resp_STK_NOSYNC);
    	send_P(200, "text/plain", (const char *)resp, 2);
    }
}

void ESP8266AVRISPWebServer::breply(uint8_t b) {
    uint8_t resp[3];
    if (Sync_CRC_EOP == getch()) {
        resp[0] = Resp_STK_INSYNC;
        resp[1] = b;
        resp[2] = Resp_STK_OK;
        //_client.write((const uint8_t *)resp, (size_t)3);
        send_P(200, "text/plain", (const char *)resp, 3);
    } else {
        error++;
        //_client.print((char)Resp_STK_NOSYNC);
        resp[0] = Resp_STK_NOSYNC;
        resp[1] = b;
        resp[2] = Resp_STK_OK;
        send_P(200, "text/plain", (const char *)resp, 3);
    }

}

void ESP8266AVRISPWebServer::get_parameter(uint8_t c) {
    switch (c) {
    case 0x80:
        breply(AVRISP_HWVER);
        break;
    case 0x81:
        breply(AVRISP_SWMAJ);
        break;
    case 0x82:
        breply(AVRISP_SWMIN);
        break;
    case 0x93:
        breply('S'); // serial programmer
        break;
    default:
        breply(0);
    }
}

void ESP8266AVRISPWebServer::set_parameters() {
    // call this after reading paramter packet into buff[]
    param.devicecode = buff[0];
    param.revision   = buff[1];
    param.progtype   = buff[2];
    param.parmode    = buff[3];
    param.polling    = buff[4];
    param.selftimed  = buff[5];
    param.lockbytes  = buff[6];
    param.fusebytes  = buff[7];
    param.flashpoll  = buff[8];
    // ignore buff[9] (= buff[8])
    // following are 16 bits (big endian)
    param.eeprompoll = beget16(&buff[10]);
    param.pagesize   = beget16(&buff[12]);
    param.eepromsize = beget16(&buff[14]);

    // 32 bits flashsize (big endian)
    param.flashsize = buff[16] * 0x01000000
                    + buff[17] * 0x00010000
                    + buff[18] * 0x00000100
                    + buff[19];
}

void ESP8266AVRISPWebServer::start_pmode() {
    SPI.begin();
    SPI.setFrequency(_spi_freq);
    SPI.setHwCs(false);

    // try to sync the bus
    SPI.transfer(0x00);
    digitalWrite(_reset_pin, _resetLevel(false));
    delayMicroseconds(50);
    digitalWrite(_reset_pin, _resetLevel(true));
    delay(30);

    spi_transaction(0xAC, 0x53, 0x00, 0x00);
    pmode = 1;
}

void ESP8266AVRISPWebServer::end_pmode() {
    SPI.end();
    setReset(_reset_state);
    pmode = 0;
}

void ESP8266AVRISPWebServer::universal() {
    int w;
    uint8_t ch;

    fill(4);
    ch = spi_transaction(buff[0], buff[1], buff[2], buff[3]);
    breply(ch);
}

void ESP8266AVRISPWebServer::flash(uint8_t hilo, int addr, uint8_t data) {
    spi_transaction(0x40 + 8 * hilo,
                    addr >> 8 & 0xFF,
                    addr & 0xFF,
                    data);
}

void ESP8266AVRISPWebServer::commit(int addr) {
    spi_transaction(0x4C, (addr >> 8) & 0xFF, addr & 0xFF, 0);
    delay(AVRISP_PTIME);
}

//#define _addr_page(x) (here & 0xFFFFE0)
int ESP8266AVRISPWebServer::addr_page(int addr) {
    if (param.pagesize == 32)  return addr & 0xFFFFFFF0;
    if (param.pagesize == 64)  return addr & 0xFFFFFFE0;
    if (param.pagesize == 128) return addr & 0xFFFFFFC0;
    if (param.pagesize == 256) return addr & 0xFFFFFF80;
    AVRISP_DEBUG("unknown page size: %d", param.pagesize);
    return addr;
}


void ESP8266AVRISPWebServer::write_flash(int length) {
    uint32_t started = millis();

    fill(length);
	
	uint8_t resp[2];

    if (Sync_CRC_EOP == getch()) {
        //_client.print((char) Resp_STK_INSYNC);
        //_client.print((char) write_flash_pages(length));
		resp[0] = Resp_STK_INSYNC;
		resp[1] = write_flash_pages(length);
		send_P(200, "text/plain", (const char *)resp, 2);
    } else {
      error++;
      //_client.print((char) Resp_STK_NOSYNC);
	  resp[0] = Resp_STK_NOSYNC;
	  send_P(200, "text/plain", (const char *)resp, 1);
    }
}

uint8_t ESP8266AVRISPWebServer::write_flash_pages(int length) {
    int x = 0;
    int page = addr_page(here);
    while (x < length) {
        yield();
        if (page != addr_page(here)) {
            commit(page);
            page = addr_page(here);
        }
        flash(LOW, here, buff[x++]);
        flash(HIGH, here, buff[x++]);
        here++;
    }
    commit(page);
    return Resp_STK_OK;
}

uint8_t ESP8266AVRISPWebServer::write_eeprom(int length) {
    // here is a word address, get the byte address
    int start = here * 2;
    int remaining = length;
    if (length > param.eepromsize) {
        error++;
        return Resp_STK_FAILED;
    }
    while (remaining > EECHUNK) {
        write_eeprom_chunk(start, EECHUNK);
        start += EECHUNK;
        remaining -= EECHUNK;
    }
    write_eeprom_chunk(start, remaining);
    return Resp_STK_OK;
}
// write (length) bytes, (start) is a byte address
uint8_t ESP8266AVRISPWebServer::write_eeprom_chunk(int start, int length) {
    // this writes byte-by-byte,
    // page writing may be faster (4 bytes at a time)
    fill(length);
    // prog_lamp(LOW);
    for (int x = 0; x < length; x++) {
        int addr = start + x;
        spi_transaction(0xC0, (addr >> 8) & 0xFF, addr & 0xFF, buff[x]);
        delay(45);
    }
    // prog_lamp(HIGH);
    return Resp_STK_OK;
}

void ESP8266AVRISPWebServer::program_page() {
    char result = (char) Resp_STK_FAILED;
    int length = 256 * getch();
    length += getch();
    char memtype = getch();
    char buf[100];
    // flash memory @here, (length) bytes
    if (memtype == 'F') {
        write_flash(length);
        return;
    }

	uint8_t resp[2];
    if (memtype == 'E') {
        result = (char)write_eeprom(length);
        if (Sync_CRC_EOP == getch()) {
            //_client.print((char) Resp_STK_INSYNC);
            //_client.print(result);
			resp[0] = Resp_STK_INSYNC;
			resp[1] = result;
			send_P(200, "text/plain", (const char *)resp, 2);
        } else {
            error++;
            //_client.print((char) Resp_STK_NOSYNC);
			resp[0] = Resp_STK_NOSYNC;
			send_P(200, "text/plain", (const char *)resp, 1);
        }
        return;
    }
    //_client.print((char)Resp_STK_FAILED);
	resp[0] = Resp_STK_NOSYNC;
	send_P(200, "text/plain", (const char *)resp, 1);
	return;

}

uint8_t ESP8266AVRISPWebServer::flash_read(uint8_t hilo, int addr) {
    return spi_transaction(0x20 + hilo * 8,
                           (addr >> 8) & 0xFF,
                           addr & 0xFF,
                           0);
}

void ESP8266AVRISPWebServer::flash_read_page(int length, uint8_t* data) {
    //uint8_t *data = (uint8_t *) malloc(length + 1);
    for (int x = 0; x < length; x += 2) {
        *(data + x) = flash_read(LOW, here);
        *(data + x + 1) = flash_read(HIGH, here);
        here++;
    }
    *(data + length) = Resp_STK_OK;
    //_client.write((const uint8_t *)data, (size_t)(length + 1));
    //free(data);
    return;
}

void ESP8266AVRISPWebServer::eeprom_read_page(int length) {
    // here again we have a word address
    uint8_t *data = (uint8_t *) malloc(length + 1);
    int start = here * 2;
    for (int x = 0; x < length; x++) {
        int addr = start + x;
        uint8_t ee = spi_transaction(0xA0, (addr >> 8) & 0xFF, addr & 0xFF, 0xFF);
        *(data + x) = ee;
    }
    *(data + length) = Resp_STK_OK;
    _client.write((const uint8_t *)data, (size_t)(length + 1));
    free(data);
    return;
}

void ESP8266AVRISPWebServer::read_page() {
    char result = (char)Resp_STK_FAILED;
    int length = 256 * getch();
    length += getch();
    char memtype = getch();
	uint8_t *data = (uint8_t *) malloc(length + 2);
    if (Sync_CRC_EOP != getch()) {
        error++;
        //_client.print((char) Resp_STK_NOSYNC);
		*data = Resp_STK_NOSYNC;
		send_P(200, "text/plain", (const char *)data, 1);
		free(data);
        return;
    }
    //_client.print((char) Resp_STK_INSYNC);
	*data = Resp_STK_INSYNC;
    if (memtype == 'F'){
		flash_read_page(length,data+1);
		send_P(200, "text/plain", (const char *)data, length + 2);
	}
    if (memtype == 'E') eeprom_read_page(length);
	free(data);
    return;
}

void ESP8266AVRISPWebServer::read_signature() {
	uint8_t resp[5];
    if (Sync_CRC_EOP != getch()) {
        error++;
        //_client.print((char) Resp_STK_NOSYNC);
		resp[0] = Resp_STK_NOSYNC;
		send_P(200, "text/plain", (const char *)resp, 1);
        return;
    }
    //_client.print((char) Resp_STK_INSYNC);
    uint8_t high = spi_transaction(0x30, 0x00, 0x00, 0x00);
    //_client.print((char) high);
    uint8_t middle = spi_transaction(0x30, 0x00, 0x01, 0x00);
    //_client.print((char) middle);
    uint8_t low = spi_transaction(0x30, 0x00, 0x02, 0x00);
    //_client.print((char) low);
    //_client.print((char) Resp_STK_OK);
	resp[0] = Resp_STK_INSYNC;
	resp[1] = high;
	resp[2] = middle;
	resp[3] = low;
	resp[4] = Resp_STK_OK;
	send_P(200, "text/plain", (const char*)resp, 5);
	Serial.print("read_signature: ");
	Serial.println((const char*)resp);
}

// It seems ArduinoISP is based on the original STK500 (not v2)
// but implements only a subset of the commands.
int ESP8266AVRISPWebServer::avrisp() {
#ifdef HTTP_ESPAVRISP_DEBUG
	Serial.print("_currentBodyIndex: ");
	Serial.println(_currentBodyIndex);
#endif
    uint8_t data, low, high;
    uint8_t ch = getch();
	char resp[9];
#ifdef HTTP_ESPAVRISP_DEBUG
    std::map<uint8_t,char*>::iterator it = cmd_debug.find(ch);
    if (it != cmd_debug.end()){
    	Serial.print("Command: ");
		Serial.println(cmd_debug[ch]);
    }
#endif
    // AVRISP_DEBUG("CMD 0x%02x", ch);
    switch (ch) {
    case Cmnd_STK_GET_SYNC:
        error = 0;
        empty_reply();
        break;

    case Cmnd_STK_GET_SIGN_ON:
        if (getch() == Sync_CRC_EOP) {
            //_client.print((char) Resp_STK_INSYNC);
            //_client.print(F("AVR ISP")); // AVR061 says "AVR STK"?
            //_client.print((char) Resp_STK_OK);
			resp[0] = Resp_STK_INSYNC;
			resp[1] = 'A';
			resp[2] = 'V';
			resp[3] = 'R';
			resp[4] = ' ';
			resp[5] = 'I';
			resp[6] = 'S';
			resp[7] = 'P';
			resp[8] = Resp_STK_INSYNC;
			send_P(200, "text/plain", resp, 9);
        }
        break;

    case Cmnd_STK_GET_PARAMETER:
        get_parameter(getch());
        break;

    case Cmnd_STK_SET_DEVICE:
        fill(20);
        set_parameters();
        empty_reply();
        break;

    case Cmnd_STK_SET_DEVICE_EXT:   // ignored
        fill(5);
        empty_reply();
        break;

    case Cmnd_STK_ENTER_PROGMODE:
        start_pmode();
        empty_reply();
        break;

    case Cmnd_STK_LOAD_ADDRESS:
        here = getch();
        here += 256 * getch();
        // AVRISP_DEBUG("here=0x%04x", here);
        empty_reply();
        break;

    // XXX: not implemented!
    case Cmnd_STK_PROG_FLASH:
        low = getch();
        high = getch();
        empty_reply();
        break;

    // XXX: not implemented!
    case Cmnd_STK_PROG_DATA:
        data = getch();
        empty_reply();
        break;

    case Cmnd_STK_PROG_PAGE:
        program_page();
        break;

    case Cmnd_STK_READ_PAGE:
        read_page();
        break;

    case Cmnd_STK_UNIVERSAL:
        universal();
        break;

    case Cmnd_STK_LEAVE_PROGMODE:
        error = 0;
        end_pmode();
        empty_reply();
        delay(5);
        // if (_client && _client.connected())
        //_client.stop();
        // AVRISP_DEBUG("left progmode");
		setReset(false);
        break;

    case Cmnd_STK_READ_SIGN:
        read_signature();
        break;
        // expecting a command, not Sync_CRC_EOP
        // this is how we can get back in sync
    case Sync_CRC_EOP:       // 0x20, space
        error++;
        //_client.print((char) Resp_STK_NOSYNC);
		resp[0] = Resp_STK_NOSYNC;
		send_P(200, "text/plain", (const char *)resp, 1);
        break;

      // anything else we will return STK_UNKNOWN
    default:
        AVRISP_DEBUG("??!?");
        error++;
        if (Sync_CRC_EOP == getch()) {
            //_client.print((char)Resp_STK_UNKNOWN);
			resp[0] = Resp_STK_NOSYNC;
			send_P(200, "text/plain", (const char *)resp, 1);
        } else {
            //_client.print((char)Resp_STK_NOSYNC);
			resp[0] = Resp_STK_NOSYNC;
			send_P(200, "text/plain", (const char *)resp, 1);
        }
  }
  _currentBodyIndex = 0;
}
