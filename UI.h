/*

  ESP32 BLE Collector - A BLE scanner with sqlite data persistence on the SD Card
  Source: https://github.com/tobozo/ESP32-BLECollector

  MIT License

  Copyright (c) 2018 tobozo

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  -----------------------------------------------------------------------------

*/

// icon positions for RTC/DB/BLE
#define ICON_RTC_X 92
#define ICON_RTC_Y 7
#define ICON_BLE_X 104
#define ICON_BLE_Y 7
#define ICON_DB_X 116
#define ICON_DB_Y 7
#define ICON_R 4
// blescan progress bar vertical position
#define PROGRESSBAR_Y 30
// sizing the UI
#define HEADER_HEIGHT 40 // Important: resulting SCROLL_HEIGHT must be a multiple of font height, default font height is 8px
#define FOOTER_HEIGHT 40 // Important: resulting SCROLL_HEIGHT must be a multiple of font height, default font height is 8px
#define SCROLL_HEIGHT ( tft.height() - ( HEADER_HEIGHT + FOOTER_HEIGHT ))

// heap map settings
#define HEAPMAP_BUFFLEN 61 // graph width (+ 1 for hscroll)
#define MAX_ROW_LEN 30 // max chars per line on display, used to position/cut text
// heap management (used by graph)
static uint32_t min_free_heap = 90000; // sql out of memory errors eventually occur under 100000
static uint32_t initial_free_heap = freeheap;
static uint32_t heap_tolerance = 20000; // how much memory under min_free_heap the sketch can go and recover without restarting itself
static uint32_t heapmap[HEAPMAP_BUFFLEN] = {0}; // stores the history of heapmap values
static byte heapindex = 0; // index in the circular buffer
static bool blinkit = false; // task blinker state
static bool blinktoggler = true;
static uint16_t blestateicon;
static unsigned long blinknow = millis(); // task blinker start time
static unsigned long scanTime = SCAN_DURATION * 1000; // task blinker duration
static unsigned long blinkthen = blinknow + scanTime; // task blinker end time
static unsigned long lastblink = millis(); // task blinker last blink
static unsigned long lastprogress = millis(); // task blinker progress

#define SPACETABS "      "
#define SPACE " "

enum TextDirections {
  ALIGN_FREE   = 0,
  ALIGN_LEFT   = 1,
  ALIGN_RIGHT  = 2,
  ALIGN_CENTER = 3,
};

// top and bottom non-scrolly zones
#define HEADER_BGCOLOR tft.color565(0x22, 0x22, 0x22)
#define FOOTER_BGCOLOR tft.color565(0x22, 0x22, 0x22)
// BLECard info styling
#define IN_CACHE_COLOR tft.color565(0x37, 0x6b, 0x37)
#define NOT_IN_CACHE_COLOR tft.color565(0xa4, 0xa0, 0x5f)
#define ANONYMOUS_COLOR tft.color565(0x88, 0x88, 0x88)
#define NOT_ANONYMOUS_COLOR tft.color565(0xee, 0xee, 0xee)
// one carefully chosen blue
#define BLUETOOTH_COLOR tft.color565(0x14, 0x54, 0xf0)
#define WROVER_DARKORANGE tft.color565(0x80, 0x40, 0x00)
// middle scrolly zone
#define BLECARD_BGCOLOR tft.color565(0x22, 0x22, 0x44)

enum BLECardThemes {
  IN_CACHE_ANON = 0,
  IN_CACHE_NOT_ANON = 1,
  NOT_IN_CACHE_ANON = 2,
  NOT_IN_CACHE_NOT_ANON = 3
};




class UIUtils {
  public:

    struct BLECardStyle {
      uint16_t textColor = WROVER_WHITE;
      uint16_t borderColor = WROVER_WHITE;
      uint16_t bgColor = BLECARD_BGCOLOR;
      void setTheme( byte themeID ) {
        bgColor = BLECARD_BGCOLOR;
        switch ( themeID ) {
          case IN_CACHE_ANON:// = 0,
            borderColor = IN_CACHE_COLOR;
            textColor = ANONYMOUS_COLOR;
            break;
          case IN_CACHE_NOT_ANON:// = 1,
            borderColor = IN_CACHE_COLOR;
            textColor = NOT_ANONYMOUS_COLOR;
            break;
          case NOT_IN_CACHE_ANON:// = 2,
            borderColor = NOT_IN_CACHE_COLOR;
            textColor = ANONYMOUS_COLOR;
            break;
          case NOT_IN_CACHE_NOT_ANON:// = 3
            borderColor = NOT_IN_CACHE_COLOR;
            textColor = NOT_ANONYMOUS_COLOR;
            break;
        }
      }
    };

    BLECardStyle BLECardTheme;

    void init() {

      bool clearScreen = true;
      if (resetReason == 12) { // SW Reset
        clearScreen = false;
      }
      tft.begin();
      tft.setRotation( 0 ); // required to get smooth scrolling
      tft.setTextColor(WROVER_YELLOW);
      if (clearScreen) {
        tft.fillScreen(WROVER_BLACK);
        tft.fillRect(0, HEADER_HEIGHT, Out.width, SCROLL_HEIGHT, BLECARD_BGCOLOR);
        // clear heap map
        for (uint16_t i = 0; i < HEAPMAP_BUFFLEN; i++) heapmap[i] = 0;
      }
      tft.fillRect(0, 0, Out.width, HEADER_HEIGHT, HEADER_BGCOLOR);
      tft.fillRect(0, Out.height - FOOTER_HEIGHT, Out.width, FOOTER_HEIGHT, FOOTER_BGCOLOR);
      tft.fillRect(0, PROGRESSBAR_Y, Out.width, 2, WROVER_GREENYELLOW);

      alignTextAt( "BLE Collector", 6, 4, WROVER_YELLOW, HEADER_BGCOLOR, ALIGN_FREE );
      if (resetReason == 12) { // SW Reset
        headerStats("Heap heap heap...");
        delay(1000);
      } else {
        headerStats("Init UI");
      }
      Out.setupScrollArea(HEADER_HEIGHT, FOOTER_HEIGHT);
      timeSetup();
      updateTimeString( true );
      timeStateIcon();
      footerStats();
      //vSemaphoreCreateBinary( xSemaphore );
      
      //taskHeapGraph();
      xTaskCreate(taskHeapGraph, "taskHeapGraph", 4096, NULL, 5, NULL);
      
      if ( clearScreen ) {
        playIntro();
      }
    }


    void update() {
      if ( freeheap + heap_tolerance < min_free_heap ) {
        headerStats("Out of heap..!");
        Serial.print("Heap too low:"); Serial.println(freeheap);
        delay(1000);
        ESP.restart();
      }
      updateTimeString();
      headerStats("");
    }


    void playIntro() {
      uint16_t pos = 0;
      tft.setTextColor(WROVER_GREENYELLOW, BGCOLOR);
      for (int i = 0; i < 5; i++) {
        pos += Out.println();
      }
      pos += Out.println("         /---------------------\\");
      pos += Out.println("         | ESP32 BLE Collector |");
      pos += Out.println("         | ------------------- |");
      pos += Out.println("         | (c+)  tobozo  2018  |");
      pos += Out.println("         \\---------------------/");
      tft.drawJpg( tbz_28x28_jpg, tbz_28x28_jpg_len, 106, Out.scrollPosY - pos + 8, 28,  28);
      for (int i = 0; i < 5; i++) {
        Out.println();
      }
      delay(5000);
    }


    void alignTextAt(const char* text, uint16_t x, uint16_t y, int16_t color = WROVER_YELLOW, int16_t bgcolor = WROVER_BLACK, byte textAlign = ALIGN_FREE) {
      tft.setTextColor(color, bgcolor);
      tft.getTextBounds(text, x, y, &Out.x1_tmp, &Out.y1_tmp, &Out.w_tmp, &Out.h_tmp);
      switch (textAlign) {
        case ALIGN_FREE:
          tft.setCursor(x, y);
          break;
        case ALIGN_LEFT:
          tft.setCursor(0, y);
          break;
        case ALIGN_RIGHT:
          tft.setCursor(Out.width - Out.w_tmp, y);
          break;
        case ALIGN_CENTER:
          tft.setCursor(Out.width / 2 - Out.w_tmp / 2, y);
          break;
      }
      tft.print(text);
    }


    void headerStats(const char *status) {
      if (isScrolling) return;
      int16_t posX = tft.getCursorX();
      int16_t posY = tft.getCursorY();
      const char* heapTpl = " Heap: %6d";
      char heapStr[16] = {'\0'};
      sprintf(heapStr, heapTpl, freeheap);
      const char* entriesTpl = " Entries:%4d";
      char entriesStr[14] = {'\0'};
      sprintf(entriesStr, entriesTpl, entries);
      
      //String s_heap = " Heap: " + String(freeheap) + " ";
      //String s_entries = " Entries: " + String(entries) + " ";
      alignTextAt( heapStr, 128, 4, WROVER_GREENYELLOW, HEADER_BGCOLOR, ALIGN_RIGHT );
      if (status && status[0] != '\0') {
        tft.fillRect(0, 18, Out.width, 8, HEADER_BGCOLOR); // clear whole message status area
        byte alignoffset = 5;
        if (strstr(status, "Inserted")) {
          tft.drawJpg(disk_jpeg, disk_jpeg_len, alignoffset, 18, 8, 8); // disk icon
          alignoffset +=10;
        } else if (strstr(status, "Cache")) {
          tft.drawJpg(ghost_jpeg, ghost_jpeg_len, alignoffset, 18, 8, 8); // disk icon
          alignoffset +=10;
        } else if (strstr(status, "DB")) {
          tft.drawJpg(moai_jpeg, moai_jpeg_len, alignoffset, 18, 8, 8); // disk icon
          alignoffset +=10;
        } else {
          
        }
        alignTextAt( status, alignoffset, 18, WROVER_YELLOW, HEADER_BGCOLOR, ALIGN_FREE );
      }
      alignTextAt( entriesStr, 128, 18, WROVER_GREENYELLOW, HEADER_BGCOLOR, ALIGN_RIGHT );
      tft.drawJpg(ram_jpeg, ram_jpeg_len, 156, 4, 8, 8); // heap icon
      tft.drawJpg(earth_jpeg, earth_jpeg_len, 156, 18, 8, 8); // entries icon
      tft.drawJpg( tbz_28x28_jpg, tbz_28x28_jpg_len, 124, 0, 28,  28); // app icon
      tft.setCursor(posX, posY);
    }


    void footerStats() {
      if (isScrolling) return;
      int16_t posX = tft.getCursorX();
      int16_t posY = tft.getCursorY();

      alignTextAt( hhmmString,   95, 288, WROVER_YELLOW, FOOTER_BGCOLOR, ALIGN_FREE );
      alignTextAt( UpTimeString, 95, 298, WROVER_YELLOW, FOOTER_BGCOLOR, ALIGN_FREE );
      alignTextAt("(c+) tobozo", 77, 308, WROVER_YELLOW, FOOTER_BGCOLOR, ALIGN_FREE );

      const char* sessDevicesCountTpl = " Total:%4d ";// + String(sessDevicesCount) + " ";
      const char* devicesCountTpl = " Last: %4d ";// + String(devicesCount) + " ";
      const char* newDevicesCountTpl = " New:  %4d ";// + String(newDevicesCount) + " ";
      char sessDevicesCountStr[16] = {'\0'};// = " Total: " + String(sessDevicesCount) + " ";
      char devicesCountStr[16] = {'\0'};// = " Last:  " + String(devicesCount) + " ";
      char newDevicesCountStr[16] = {'\0'};// = " New:   " + String(newDevicesCount) + " ";

      sprintf( sessDevicesCountStr, sessDevicesCountTpl, sessDevicesCount );
      sprintf( devicesCountStr, devicesCountTpl, devicesCount );
      sprintf( newDevicesCountStr, newDevicesCountTpl, newDevicesCount );

      //String sessDevicesCountStr = " Total: " + String(sessDevicesCount) + " ";
      //String devicesCountStr = " Last:  " + String(devicesCount) + " ";
      //String newDevicesCountStr = " New:   " + String(newDevicesCount) + " ";
      alignTextAt( devicesCountStr, 0, 288, WROVER_GREENYELLOW, FOOTER_BGCOLOR, ALIGN_LEFT );
      alignTextAt( sessDevicesCountStr, 0, 298, WROVER_GREENYELLOW, FOOTER_BGCOLOR, ALIGN_LEFT );
      alignTextAt( newDevicesCountStr, 0, 308, WROVER_GREENYELLOW, FOOTER_BGCOLOR, ALIGN_LEFT );

      tft.setCursor(posX, posY);
    }


    void cacheStats(byte BLEDevCacheUsed, byte VendorCacheUsed, byte OuiCacheUsed) {
      percentBox(164, 284, 10, 10, BLEDevCacheUsed, WROVER_CYAN, WROVER_BLACK);
      percentBox(164, 296, 10, 10, VendorCacheUsed, WROVER_ORANGE, WROVER_BLACK);
      percentBox(164, 308, 10, 10, OuiCacheUsed, WROVER_GREENYELLOW, WROVER_BLACK);
    }


    void percentBox(uint16_t x, uint16_t y, byte w, byte h, byte percent, uint16_t barcolor, uint16_t bgcolor, uint16_t bordercolor = WROVER_DARKGREY) {
      if (percent == 0) {
        tft.drawRect(x - 1, y - 1, w + 2, h + 2, bordercolor);
        tft.fillRect(x, y, w, h, bgcolor);
        return;
      }
      if (percent == 100) {
        tft.drawRect(x - 1, y - 1, w + 2, h + 2, BLUETOOTH_COLOR);
        tft.fillRect(x, y, w, h, barcolor);
        return;
      }
      tft.drawRect(x - 1, y - 1, w + 2, h + 2, bordercolor);
      tft.fillRect(x, y, w, h, bgcolor);
      byte yoffsetpercent = percent / 10;
      byte boxh = (yoffsetpercent * h) / 10 ;
      tft.fillRect(x, y, w, boxh, barcolor);

      byte xoffsetpercent = percent % 10;
      if (xoffsetpercent == 0) return;
      byte linew = (xoffsetpercent * w) / 10;
      tft.drawFastHLine(x, y + boxh, linew, barcolor);

    }


    static void dbStateIcon(int state) {
      uint16_t color = WROVER_DARKGREY;
      switch (state) {
        case 2:/*DB OPEN FOR WRITING*/
          color = WROVER_ORANGE;
          break;
        case 1/*DB_OPEN FOR READING*/:
          color = WROVER_YELLOW;
          break;
        case 0/*DB CLOSED*/:
          color = WROVER_DARKGREEN;
          break;
        case -1/*DB BROKEN*/:
          color = WROVER_RED;
          break;
      }
      tft.fillCircle(ICON_DB_X, ICON_DB_Y, ICON_R, color);
    }


    static void timeStateIcon() {

      tft.drawJpg( clock_jpeg, clock_jpeg_len, ICON_RTC_X-ICON_R, ICON_RTC_Y-ICON_R+1, 8,  8);
      
      //tft.fillCircle(ICON_RTC_X, ICON_RTC_Y, ICON_R, WROVER_GREENYELLOW);
      if (RTC_is_running) {
        //tft.drawCircle(ICON_RTC_X, ICON_RTC_Y, ICON_R, WROVER_DARKGREEN);
        //tft.drawFastHLine(ICON_RTC_X, ICON_RTC_Y, ICON_R, WROVER_DARKGREY);
        //tft.drawFastVLine(ICON_RTC_X, ICON_RTC_Y, ICON_R - 2, WROVER_DARKGREY);
      } else {
        tft.drawCircle(ICON_RTC_X, ICON_RTC_Y, ICON_R, WROVER_RED);
        //tft.drawFastHLine(ICON_RTC_X, ICON_RTC_Y, ICON_R, WROVER_RED);
        //tft.drawFastVLine(ICON_RTC_X, ICON_RTC_Y, ICON_R - 2, WROVER_RED);
      }
    }


    static void bleStateIcon(uint16_t color, bool fill = true) {
      blestateicon = color;
      if (fill) {
        tft.fillCircle(ICON_BLE_X, ICON_BLE_Y, ICON_R, color);
      } else {
        tft.fillCircle(ICON_BLE_X, ICON_BLE_Y, ICON_R - 1, color);
      }
    }


    static void blinkIcon() {
      #if SCAN_MODE==SCAN_TASK_0 || SCAN_MODE==SCAN_TASK_1 || SCAN_MODE==SCAN_TASK
        xSemaphoreTake(mux, portMAX_DELAY);
      #endif
      
      if (!blinkit || blinknow >= blinkthen) {
        blinkit = false;
        if(blestateicon!=WROVER_DARKGREY) {
          tft.fillRect(0, PROGRESSBAR_Y, Out.width, 2, WROVER_DARKGREY);
          // clear blue pin
          bleStateIcon(WROVER_DARKGREY);
        }
        #if SCAN_MODE==SCAN_TASK_0 || SCAN_MODE==SCAN_TASK_1 || SCAN_MODE==SCAN_TASK
          xSemaphoreGive( mux );
        #endif
        return;
      }
      blinknow = millis();
      if (lastblink + random(222, 666) < blinknow) {
        blinktoggler = !blinktoggler;
        if (blinktoggler) {
          bleStateIcon(BLUETOOTH_COLOR);
        } else {
          bleStateIcon(HEADER_BGCOLOR, false);
        }
        lastblink = blinknow;
      }
      if (lastprogress + 1000 < blinknow) {
        unsigned long remaining = blinkthen - blinknow;
        int percent = 100 - ( ( remaining * 100 ) / scanTime );
        tft.fillRect(0, PROGRESSBAR_Y, (Out.width * percent) / 100, 2, BLUETOOTH_COLOR);
        lastprogress = blinknow;
      }
      #if SCAN_MODE==SCAN_TASK_0 || SCAN_MODE==SCAN_TASK_1 || SCAN_MODE==SCAN_TASK
        xSemaphoreGive( mux );
      #endif
    }

    /* spawn subtasks and leave */
    static void taskHeapGraph( void * pvParameters ) { // always running
      #if SCAN_MODE==SCAN_TASK_0 || SCAN_MODE==SCAN_TASK_1 || SCAN_MODE==SCAN_TASK
        mux = xSemaphoreCreateMutex();
        xTaskCreate(heapGraph, "HeapGraph", 2000, NULL, 5, NULL);
      #else
        xTaskCreatePinnedToCore(heapGraph, "HeapGraph", 2000, (void *)1, 0, NULL, 0); /* last = Task Core */
      #endif
      vTaskDelete(NULL);
    }


    static void heapGraph(void * parameter) {
      uint32_t lastfreeheap;
      uint32_t toleranceheap = min_free_heap + heap_tolerance;
      uint8_t i = 0;
      uint32_t GRAPH_LINE_WIDTH = HEAPMAP_BUFFLEN - 1;
      uint32_t GRAPH_LINE_HEIGHT = 35;
      uint16_t GRAPH_X = Out.width - GRAPH_LINE_WIDTH - 2;
      uint16_t GRAPH_Y = 283;
      while (1) {
        if (isScrolling) {
          delay(30);
          continue;
        }
        
        if (lastfreeheap != freeheap) {
          heapmap[heapindex++] = freeheap;
          heapindex = heapindex % HEAPMAP_BUFFLEN;
          lastfreeheap = freeheap;
        } else {
          blinkIcon();
          delay(30);
          continue;
        }
        #if SCAN_MODE==SCAN_TASK_0 || SCAN_MODE==SCAN_TASK_1 || SCAN_MODE==SCAN_TASK
        if( xSemaphoreTake(mux, portMAX_DELAY)!=pdTRUE ) {
          continue;
        }
        #endif
        
        uint16_t GRAPH_COLOR = WROVER_WHITE;
        uint32_t graphMin = min_free_heap;
        uint32_t graphMax = graphMin;
        uint32_t toleranceline = GRAPH_LINE_HEIGHT;
        uint32_t minline = 0;
        uint16_t GRAPH_BG_COLOR = WROVER_BLACK;
        // dynamic scaling
        for (i = 0; i < GRAPH_LINE_WIDTH; i++) {
          int thisindex = int(heapindex - GRAPH_LINE_WIDTH + i + HEAPMAP_BUFFLEN) % HEAPMAP_BUFFLEN;
          uint32_t heapval = heapmap[thisindex];
          if (heapval != 0 && heapval < graphMin) {
            graphMin =  heapval;
          }
          if (heapval > graphMax) {
            graphMax = heapval;
          }
        }
        /* min anx max lines */
        if (graphMin != graphMax) {
          toleranceline;// = map(toleranceheap, graphMin, graphMax, 0, GRAPH_LINE_HEIGHT);
          minline = map(min_free_heap, graphMin, graphMax, 0, GRAPH_LINE_HEIGHT);
          if (toleranceheap > graphMax) {
            GRAPH_BG_COLOR = WROVER_ORANGE;
            toleranceline = GRAPH_LINE_HEIGHT;
          } else if ( toleranceheap < graphMin ) {
            toleranceline = 0;
          } else {
            toleranceline = map(toleranceheap, graphMin, graphMax, 0, GRAPH_LINE_HEIGHT);
          }
        }
        // draw graph
        for (i = 0; i < GRAPH_LINE_WIDTH; i++) {
          int thisindex = int(heapindex - GRAPH_LINE_WIDTH + i + HEAPMAP_BUFFLEN) % HEAPMAP_BUFFLEN;
          uint32_t heapval = heapmap[thisindex];
          if ( heapval > toleranceheap ) {
            // nominal
            GRAPH_COLOR = WROVER_GREEN;
            GRAPH_BG_COLOR = WROVER_DARKGREY;
          } else {
            if ( heapval > min_free_heap ) {
              // in tolerance zone
              GRAPH_COLOR = WROVER_YELLOW;
              GRAPH_BG_COLOR = WROVER_DARKGREEN;
            } else {
              // under tolerance zone
              if (heapval > 0) {
                GRAPH_COLOR = WROVER_RED;
                GRAPH_BG_COLOR = WROVER_ORANGE;
              } else {
                // no record
                GRAPH_BG_COLOR = WROVER_BLACK;
              }
            }
          }
          // fill background
          tft.drawFastVLine( GRAPH_X + i, GRAPH_Y, GRAPH_LINE_HEIGHT, GRAPH_BG_COLOR );
          if ( heapval > 0 ) {
            uint32_t lineheight = map(heapval, graphMin, graphMax, 0, GRAPH_LINE_HEIGHT);
            tft.drawFastVLine( GRAPH_X + i, GRAPH_Y + GRAPH_LINE_HEIGHT-lineheight, lineheight, GRAPH_COLOR );
          }
        }
        if (graphMin != graphMax) {
          //uint32_t toleranceline = map(min_free_heap + heap_tolerance, graphMin, graphMax, 0, GRAPH_LINE_HEIGHT);
          //uint32_t minline = map(min_free_heap, graphMin, graphMax, 0, GRAPH_LINE_HEIGHT);
          tft.drawFastHLine( GRAPH_X, GRAPH_Y + GRAPH_LINE_HEIGHT - toleranceline, GRAPH_LINE_WIDTH, WROVER_LIGHTGREY );
          tft.drawFastHLine( GRAPH_X, GRAPH_Y + GRAPH_LINE_HEIGHT - minline, GRAPH_LINE_WIDTH, WROVER_RED );
        }
        
        //blinkIcon();
        #if SCAN_MODE==SCAN_TASK_0 || SCAN_MODE==SCAN_TASK_1 || SCAN_MODE==SCAN_TASK
          xSemaphoreGive( mux );
        #endif
      }
    }


    static void startBlink() { // runs one and detaches
      tft.drawJpg( zzz_jpeg, zzz_jpeg_len, 5, 18, 8,  8 );
      blinkit = true;
      blinknow = millis();
      scanTime = SCAN_DURATION * 1000;
      blinkthen = blinknow + scanTime;
      lastblink = millis();
      lastprogress = millis();
    }

    static void stopBlink() {
      // clear progress bar
      blinkit = false;
      delay(150); // give some time to the task to end
    }


    static bool BLECardIsOnScreen(const char* address) {
      bool onScreen = false;
      //Serial.print("Checking onScreen: "); Serial.println( address );
      for (int j = 0; j < BLECARD_MAC_CACHE_SIZE; j++) {
        if ( strcmp(address, lastPrintedMac[j]) == 0) {
          //Serial.println("is onScreen!");
          onScreen = true;
          break;
        }
      }
      return onScreen;
    }


    int printBLECard(byte cacheindex) {

      //Serial.println("BLECARD got semaphore !");
      
      uint16_t randomcolor = tft.color565(random(128, 255), random(128, 255), random(128, 255));
      uint16_t pos = 0;
      uint16_t hop;
      tft.setTextColor(BLECardTheme.textColor, BLECardTheme.bgColor);
      BGCOLOR = BLECardTheme.bgColor;
      hop = Out.println(SPACE);
      pos += hop;
      if (BLEDevCache[cacheindex].address && BLEDevCache[cacheindex].address[0] != '\0') {
        memcpy(lastPrintedMac[lastPrintedMacIndex++ % BLECARD_MAC_CACHE_SIZE], BLEDevCache[cacheindex].address, MAC_LEN+1);
        const char *addressTpl = "  %s";
        char addressStr[24] = {'\0'};
        sprintf( addressStr, addressTpl, BLEDevCache[cacheindex].address);
        hop = Out.println( addressStr );
        pos += hop;
        char dbmStr[16];
        const char* dbmTpl = "%d dBm    ";
        sprintf(dbmStr, dbmTpl, BLEDevCache[cacheindex].rssi);
        alignTextAt( dbmStr, 0, Out.scrollPosY - hop, BLECardTheme.textColor, BLECardTheme.bgColor, ALIGN_RIGHT );
        tft.setCursor(0, Out.scrollPosY);
        drawRSSI(Out.width - 18, Out.scrollPosY - hop - 1, BLEDevCache[cacheindex].rssi, BLECardTheme.textColor);
        if (BLEDevCache[cacheindex].in_db) { // 'already seen this' icon
          tft.drawJpg( update_jpeg, update_jpeg_len, 138, Out.scrollPosY - hop, 8,  8);
        } else { // 'just inserted this' icon
          tft.drawJpg( insert_jpeg, insert_jpeg_len, 138, Out.scrollPosY - hop, 8,  8);
        }
        if (BLEDevCache[cacheindex].uuid[0] != '\0') { // 'has service UUID' Icon
          tft.drawJpg( service_jpeg, service_jpeg_len, 128, Out.scrollPosY - hop, 8,  8);
        }
      }
      if (BLEDevCache[cacheindex].ouiname[0] != '\0') {
        pos += Out.println(SPACE);
        const char* ouiTpl = "      %s";
        char ouiStr[38] = {'\0'};
        sprintf( ouiStr, ouiTpl, BLEDevCache[cacheindex].ouiname );
        hop = Out.println( ouiStr );
        pos += hop;
        tft.drawJpg( nic16_jpeg, nic16_jpeg_len, 10, Out.scrollPosY - hop, 13, 8);
      }
      if (BLEDevCache[cacheindex].appearance != 0) {
        pos += Out.println(SPACE);
        const char* appearanceTpl = "  Appearance: %d";
        char appearanceStr[48];
        sprintf(appearanceStr, appearanceTpl, BLEDevCache[cacheindex].appearance);
        hop = Out.println( appearanceStr );
        pos += hop;
      }
      if (BLEDevCache[cacheindex].name[0] != '\0') {
        const char* nameTpl = "      %s";
        char nameStr[38] = {'\0'};
        sprintf(nameStr, nameTpl, BLEDevCache[cacheindex].name);
        pos += Out.println(SPACE);
        hop = Out.println( nameStr );
        pos += hop;
        tft.drawJpg( name_jpeg, name_jpeg_len, 12, Out.scrollPosY - hop, 7,  8);
      }
      if (BLEDevCache[cacheindex].manufname[0] != '\0') {
        pos += Out.println(SPACE);
        const char* manufTpl = "      %s";
        char manufStr[38] = {'\0'};
        sprintf(manufStr, manufTpl, BLEDevCache[cacheindex].manufname);
        hop = Out.println( manufStr );
        pos += hop;
        if (strstr(BLEDevCache[cacheindex].manufname, "Apple")) {
          tft.drawJpg( apple16_jpeg, apple16_jpeg_len, 12, Out.scrollPosY - hop, 8,  8);
        } else if (strstr(BLEDevCache[cacheindex].manufname, "IBM")) {
          tft.drawJpg( ibm8_jpg, ibm8_jpg_len, 10, Out.scrollPosY - hop, 20,  8);
        } else if (strstr(BLEDevCache[cacheindex].manufname, "Microsoft")) {
          tft.drawJpg( crosoft_jpeg, crosoft_jpeg_len, 12, Out.scrollPosY - hop, 8,  8);
        } else if (strstr(BLEDevCache[cacheindex].manufname, "Bose")) {
          tft.drawJpg( speaker_icon_jpg, speaker_icon_jpg_len, 12, Out.scrollPosY - hop, 6,  8);
        } else {
          tft.drawJpg( generic_jpeg, generic_jpeg_len, 12, Out.scrollPosY - hop, 8,  8);
        }
      }
      hop = Out.println(SPACE);
      pos += hop;
      drawRoundRect(pos, 4, BLECardTheme.borderColor);
      return pos;
    }


  private:

    /* draw rounded corners boxes over the scroll limit */
    void drawRoundRect(uint16_t pos, uint16_t radius, uint16_t color) {
      if (Out.scrollPosY - pos >= Out.scrollTopFixedArea) {
        // no scroll loop point overlap, just render the box
        tft.drawRoundRect(1, Out.scrollPosY - pos + 1, Out.width - 2, pos - 2, 4, color);
      } else {
        // last block overlaps scroll loop point and has been split
        int h1 = (Out.scrollTopFixedArea - (Out.scrollPosY - pos));
        int h2 = pos - h1;
        int lwidth = Out.width - 2;
        h1 -= 2; //margin
        h2 -= 2;; //margin
        int vpos1 = Out.scrollPosY - pos + Out.yArea + 1;
        int vpos2 = Out.scrollPosY - 2;
        tft.drawFastHLine(1 + radius, vpos1, lwidth - 2 * radius, color); // upper hline
        tft.drawFastHLine(1 + radius, vpos2, lwidth - 2 * radius, color); // lower hline
        if (h1 > radius) {
          tft.drawFastVLine(1,      vpos1 + radius, h1 - radius + 1, color); // upper left vline
          tft.drawFastVLine(lwidth, vpos1 + radius, h1 - radius + 1, color); // upper right vline
        }
        if (h2 > radius) {
          tft.drawFastVLine(1,      vpos2 - h2, h2 - radius + 1, color); // lower left vline
          tft.drawFastVLine(lwidth, vpos2 - h2, h2 - radius + 1, color); // lower right vline
        }
        tft.startWrite();//BLEDev.borderColor
        tft.drawCircleHelper(1 + radius,      vpos1 + radius, radius, 1, color); // upper left
        tft.drawCircleHelper(lwidth - radius, vpos1 + radius, radius, 2, color); // upper right
        tft.drawCircleHelper(lwidth - radius, vpos2 - radius, radius, 4, color); // lower right
        tft.drawCircleHelper(1 + radius,      vpos2 - radius, radius, 8, color); // lower left
        tft.endWrite();
      }
    }

    // draws a RSSI Bar for the BLECard
    void drawRSSI(int16_t x, int16_t y, int16_t rssi, uint16_t bgcolor) {
      uint16_t barColors[4];
      if (rssi >= -30) {
        // -30 dBm and more Amazing    - Max achievable signal strength. The client can only be a few feet
        // from the AP to achieve this. Not typical or desirable in the real world.  N/A
        barColors[0] = WROVER_GREEN;
        barColors[1] = WROVER_GREEN;
        barColors[2] = WROVER_GREEN;
        barColors[3] = WROVER_GREEN;
      } else if (rssi >= -67) {
        // between -67 dBm and 31 dBm  - Very Good   Minimum signal strength for applications that require
        // very reliable, timely delivery of data packets.   VoIP/VoWiFi, streaming video
        barColors[0] = WROVER_GREEN;
        barColors[1] = WROVER_GREEN;
        barColors[2] = WROVER_GREEN;
        barColors[3] = bgcolor;
      } else if (rssi >= -70) {
        // between -70 dBm and -68 dBm - Okay  Minimum signal strength for reliable packet delivery.   Email, web
        barColors[0] = WROVER_YELLOW;
        barColors[1] = WROVER_YELLOW;
        barColors[2] = WROVER_YELLOW;
        barColors[3] = bgcolor;
      } else if (rssi >= -80) {
        // between -80 dBm and -71 dBm - Not Good  Minimum signal strength for basic connectivity. Packet delivery may be unreliable.  N/A
        barColors[0] = WROVER_YELLOW;
        barColors[1] = WROVER_YELLOW;
        barColors[2] = bgcolor;
        barColors[3] = bgcolor;
      } else if (rssi >= -90) {
        // between -90 dBm and -81 dBm - Unusable  Approaching or drowning in the noise floor. Any functionality is highly unlikely.
        barColors[0] = WROVER_RED;
        barColors[1] = bgcolor;
        barColors[2] = bgcolor;
        barColors[3] = bgcolor;
      }  else {
        // dude, this sucks
        barColors[0] = WROVER_RED; // want: WROVER_RAINBOW
        barColors[1] = bgcolor;
        barColors[2] = bgcolor;
        barColors[3] = bgcolor;
      }
      tft.fillRect(x,     y + 4, 2, 4, barColors[0]);
      tft.fillRect(x + 3, y + 3, 2, 5, barColors[1]);
      tft.fillRect(x + 6, y + 2, 2, 6, barColors[2]);
      tft.fillRect(x + 9, y + 1, 2, 7, barColors[3]);
    }
};


UIUtils UI;
