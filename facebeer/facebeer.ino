#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

// CONFIG Wi-Fi
const char* ssid = "Gabriel-2.4Ghz";
const char* password = "amaral123";

// Servo da torneira principal
Servo torneira;
const int servoPin = 2;  // D4
int posFechada = 45;     // posição inicial "fechada"
int posAberta = 160;     // posição para abrir
int posTorneira = 45;    // posição atual da torneira

// Servo da válvula de vazão
Servo vazaoServo;
const int vazaoPin = 14; // D5
int posVazao = 0;        // posição inicial
int posMin = 2;
int posMax = 178;

// Sensor de vazão
volatile long pulses = 0;
const int sensorPin = 5;  // D1
float fatorCalibracao = 1000.0; // pulsos por litro (ajustar conforme sensor)

// Web server
ESP8266WebServer server(80);

// ISR do sensor
void IRAM_ATTR contarPulso() {
  pulses++;
}

void moverServoSuave(Servo &servo, int posAtual, int posFinal, int passo = 1, int atraso = 20) {
  if (posFinal > posAtual) {
    for (int p = posAtual; p <= posFinal; p += passo) {
      servo.write(p);
      delay(atraso);
    }
  } else {
    for (int p = posAtual; p >= posFinal; p -= passo) {
      servo.write(p);
      delay(atraso);
    }
  }
}

void abrirTorneira() {
  moverServoSuave(torneira, posTorneira, posAberta, 1, 30);
  posTorneira = posAberta;
}

void fecharTorneira() {
  moverServoSuave(torneira, posTorneira, posFechada, 1, 30);
  posTorneira = posFechada;
}

// Retorna litros acumulados desde último reset
float getLitros() {
  noInterrupts();
  long count = pulses;
  interrupts();
  return (count / fatorCalibracao);
}

// Reset pulsos
void resetPulsos() {
  noInterrupts();
  pulses = 0;
  interrupts();
}

// ---- LÓGICA DE AÇÕES ----

void servir_ml(float ml) {
  Serial.println("[SERVIR] Requisitado " + String(ml) + " mL");
  resetPulsos();
  abrirTorneira();
  Serial.println("[SERVIR] Torneira aberta");

  float litros_alvo = ml / 1000.0;
  unsigned long lastLog = millis();

  while (getLitros() < litros_alvo) {
    delay(50);
    if (millis() - lastLog >= 500) {
      Serial.println("[SERVIR] Já passou " + String(getLitros() * 1000.0, 1) + " mL");
      lastLog = millis();
    }
  }

  fecharTorneira();
  Serial.println("[SERVIR] Torneira fechada");
  Serial.println("[SERVIR] Finalizado, servido " + String(ml) + " mL");
}

void sangria() {
  Serial.println("[SANGRIA] Iniciando sangria (10s)...");
  abrirTorneira();
  delay(10000);
  fecharTorneira();
  Serial.println("[SANGRIA] Finalizado");
}

void ajustarTorneiraOffset(int offset) {
  posFechada += offset;
  if (posFechada < 0) posFechada = 0;
  if (posFechada > 180) posFechada = 180;
  torneira.write(posFechada);
  Serial.println("[TORNEIRA] Offset aplicado, nova posFechada=" + String(posFechada));
}

void setTorneiraFechada(int valor) {
  posFechada = constrain(valor, 0, 180);
  torneira.write(posFechada);
  posTorneira = posFechada; // corrigido
  Serial.println("[TORNEIRA] Set posFechada=" + String(posFechada));
}

void ajustarVazaoOffset(int offset) {
  posVazao += offset;
  posVazao = constrain(posVazao, posMin, posMax);
  vazaoServo.write(posVazao);
  Serial.println("[VAZAO] Offset aplicado, nova posVazao=" + String(posVazao));
}

void setVazao(int valor) {
  posVazao = constrain(valor, posMin, posMax);
  vazaoServo.write(posVazao);
  Serial.println("[VAZAO] Set posVazao=" + String(posVazao));
}

// ---- SETUP ----

void setup() {
  Serial.begin(9600);

  // Servos
  torneira.attach(servoPin);
  torneira.write(posFechada);

  vazaoServo.attach(vazaoPin);
  vazaoServo.write(posVazao);

  // Sensor
  pinMode(sensorPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(sensorPin), contarPulso, RISING);

  // Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] Conectado!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());

  // ---- ENDPOINTS ----
  server.on("/servir", []() {
    Serial.println("[HTTP] /servir chamado");
    if (server.hasArg("ml")) {
      float ml = server.arg("ml").toFloat();
      if (ml > 0) {
        server.send(200, "text/plain", "Servindo " + String(ml) + " mL...");
        servir_ml(ml);
      } else {
        server.send(400, "text/plain", "Quantidade invalida");
      }
    } else {
      server.send(400, "text/plain", "Parametro 'ml' nao informado");
    }
  });

  server.on("/sangria", []() {
    Serial.println("[HTTP] /sangria chamado");
    server.send(200, "text/plain", "Sangria iniciada (10s)...");
    sangria();
  });

  server.on("/torneira", []() {
    Serial.println("[HTTP] /torneira chamado");
    if (server.hasArg("offset")) {
      int offset = server.arg("offset").toInt();
      ajustarTorneiraOffset(offset);
      server.send(200, "text/plain", "Torneira offset " + String(offset));
    } else if (server.hasArg("set")) {
      int valor = server.arg("set").toInt();
      setTorneiraFechada(valor);
      server.send(200, "text/plain", "Torneira set " + String(valor));
    } else {
      server.send(400, "text/plain", "Parametros aceitos: offset ou set");
    }
  });

  server.on("/vazao", []() {
    Serial.println("[HTTP] /vazao chamado");
    if (server.hasArg("offset")) {
      int offset = server.arg("offset").toInt();
      ajustarVazaoOffset(offset);
      server.send(200, "text/plain", "Vazao offset " + String(offset));
    } else if (server.hasArg("set")) {
      int valor = server.arg("set").toInt();
      setVazao(valor);
      server.send(200, "text/plain", "Vazao set " + String(valor));
    } else {
      server.send(400, "text/plain", "Parametros aceitos: offset ou set");
    }
  });

  // Healthcheck
  server.on("/", []() {
    Serial.println("[HTTP] / (healthcheck) chamado");
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  Serial.println("[HTTP] Servidor iniciado");
}

void loop() {
  server.handleClient();
}
