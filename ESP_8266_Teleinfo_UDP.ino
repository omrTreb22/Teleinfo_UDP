#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include "Wifi.h"

const char* ssid = WIFI_SSID;
const char* pass = WIFI_PASSWORD;

#define MAX_DURATION 12   // 1 value every 5s covering 1 minute
#define MAX_DATA     50
#define FIRST_INDEX(a)  (a  == (MAX_DURATION-1) ? 0 : a+1)
#define PREVIOUS_INDEX(a) (a == 0 ? (MAX_DURATION-1) : a-1)
#define INCREMENT(a)      (a >= MAX_DATA ? MAX_DATA : a+1)

unsigned long compteur[MAX_DURATION];
unsigned int puissance[MAX_DURATION];
unsigned long currentTime;
int currentIndex;
int G_sequence = 0;
int G_lastCpt = 0;

char etiquette[MAX_DATA+1];
char donnees[MAX_DATA+1];
int  rxCount;
unsigned char state = 0;
unsigned int cptBase = 0;
unsigned int  pApp = 0;
unsigned int checksum;

WiFiServer server(80);
IPAddress ip(192, 168, 1, 77);
int UDPport = 5005;


#define STATE_0_WAIT_02       0   // Attente du debut de Trame 0x02
#define STATE_1_WAIT_0A       1   // Attente du debut de ligne 0x0A
#define STATE_2_ETIQUETTE     2   // Attente de l'etiquette jusque 0x20
#define STATE_3_DONNEES       3   // Attente des donnees jusque 0x20
#define STATE_4_WAIT_CHECKSUM 4   // Attente du checksum
#define STATE_5_WAIT_0D_03    5   // Attente de fin de Trame

void handleLine(char *etiquette, char *donnees)
{
    String val = donnees;

    if (strcmp(etiquette, "EAST") == 0)
    {
        cptBase = val.toInt();
        //Serial.print(etiquette); Serial.print(" : "); Serial.print(val); Serial.print(" - "); Serial.println(cptBase);
    }
    if (strcmp(etiquette, "SINSTS") == 0)
    {
        pApp = val.toInt();
        //Serial.print(etiquette); Serial.print(" : "); Serial.print(val); Serial.print(" - "); Serial.println(pApp);
    }
    // EAIT and SINSTI nondisponible sur Linky en mode Consommateur
    //if (strcmp(etiquette, "EAIT") == 0)
    //{
    //    indexInjection = atol(donnees);
    //}
    //if (strcmp(etiquette, "SINSTI") == 0)
    //{
    //    pAppInjectee = atoi(donnees);
    //}
}

void setup() 
{
    Serial.begin(9600);
    Serial.println("");
    Serial.println("Arduino Web Server over TCP");

    WiFi.begin(ssid, pass);
    while (WiFi.waitForConnectResult() != WL_CONNECTED);

    IPAddress local_ip = WiFi.localIP();
    Serial.print("IP address: ");
    Serial.println(local_ip);
    Serial.println("Web Server is ready");

    Serial.println("Serveur Web demarre...");
    Serial.flush();
    Serial.end();

    //server.begin();
    Serial.setRxBufferSize(4096);
    Serial.begin(9600);
    
    rxCount = 0;

    currentTime = millis();
    currentIndex = 0;
    G_sequence = 0;
}

void loop()
{
    char c = 0;
    WiFiUDP Udp;

    if (Serial.available())
        c = Serial.read() & 0x7F;
           
    if (c)
        {
          //printf("Loop : Caractere %02x '%c' - state=%d rxCount=%d ", c, (c > 60) ? c : ' ', state, rxCount);
        switch(state)
        {
            case STATE_0_WAIT_02 :
                if (c == 0x02)
                    state++;
                break;
            case STATE_1_WAIT_0A :
                if (c == 0x0A)
                    {
                    state++;
                    checksum = 0;
                    rxCount = 0;
                    }
                if (c == 0x03)
                {
                    if (cptBase >= G_lastCpt)
                    {
                        G_lastCpt = cptBase;
                        puissance[currentIndex] = pApp;
                        compteur[currentIndex] = cptBase;
                    }
                    state = 0;
                }
                break;
            case STATE_2_ETIQUETTE :
                if (c == 0x09)
                {
                    state++;
                    etiquette[rxCount] = 0;
                    checksum += 9;
                    rxCount = 0;
                }
                else
                {
                    etiquette[rxCount] = c;
                    rxCount = INCREMENT(rxCount);
                    checksum += c;
                }
                break;
            case STATE_3_DONNEES :
                if (c == 0x09)
                {
                    state++;
                    checksum += 9;
                    donnees[rxCount] = 0;
                    handleLine(etiquette, donnees);
                }
                else
                {
                    donnees[rxCount] = c;
                    checksum += c;
                    rxCount = INCREMENT(rxCount);
                }
                break;
            case STATE_4_WAIT_CHECKSUM:
                checksum = checksum & 63;
                checksum += 32;
                if (c == checksum)
                {
                    handleLine(etiquette, donnees);
                }
                state++;
                break;
            case STATE_5_WAIT_0D_03:
                if (c == 0x0D)
                {
                    state = 1;
                }
                if (c == 0x03)
                {
                    state = 0;
                }
                break;
            default:
               break;
        }
    }

    if ((millis() - currentTime) >= 5000 && compteur[currentIndex] > 0) // Every 5s
    {
        currentTime = millis();
    
        if (Udp.beginPacket(ip, UDPport) != 0)
        {
            char tmp[30];

            tmp[0]='T';
            tmp[1]='I';
            tmp[2]='C';
            tmp[3]='S';
            *(unsigned int *)&tmp[4]  = compteur[currentIndex];
            *(unsigned int *)&tmp[8]  = puissance[currentIndex];
            *(unsigned int *)&tmp[12] = G_sequence; 
            tmp[16] = 0;
            Udp.write(tmp, 17);
            Udp.endPacket();
        }
        Serial.print("Send P="); Serial.print(puissance[currentIndex]); 
        Serial.print(" Index="); Serial.print(compteur[currentIndex]);
        Serial.print(" Seq="); Serial.print(G_sequence);
        Serial.print(" curIdx="); Serial.println(currentIndex);

        G_sequence++;
        int previousIndex = currentIndex;
        currentIndex++;
        if (currentIndex == MAX_DURATION)
            currentIndex = 0;
        compteur[currentIndex]  = compteur[previousIndex];
        puissance[currentIndex] = puissance[previousIndex];
    }

    return;
}
