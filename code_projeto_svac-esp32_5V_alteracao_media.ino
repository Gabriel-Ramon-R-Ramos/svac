#include <BluetoothSerial.h>
#include "HX711.h"

// DEFINIÇÕES
#define SENSOR_ETANOL 18                      // Para sensor de etanol
#define MAX 10
#define DOUT 26                               // HX711 data
#define CLK 25                                // HX711 clock
#define PINO_FLUXO 27                         // Para o sensor de fluxo
#define CALIBRACAO_FLUXO 4.5                  // Valor de calibracao do sensor de fluxo

// VARIAVEIS GLOBAIS
HX711 scale(DOUT, CLK);                       // Objeto HX711
BluetoothSerial SerialBT;                     // Objeto Bluetooth
int leiturasE[MAX];                           // Array para armazenar os valores lidos
// Variáveis para a celula de carga
float peso;
float fatorDeCalibracao = 55500;             // Calibração, ajuste conforme necessário 105494.33 ou 106740
int maiorPeso = 0;
// Variáveis para o Sensor de etanol
const float minFrequencia = 57.67;            // Frequência base (0% etanol)
const float maxFrequencia = 157.6;            // Frequência máxima (100% etanol)
volatile unsigned long ultimoPulso = 0;       // Armazena o tempo do último pulso
volatile unsigned long tempoEntrePulsos = 0;  // Tempo entre pulsos em microssegundos
volatile float frequencia = 0;                // Frequência calculada (em Hz)
float porcentagemEtanol = 0;                  // Para calculo da % de Etanol
float porcentagemGasolina = 0;                // Para calculo da % de Gasolina
float media = 0;                              // Media das leituras do sensor de etanol
float maiorValorEtanol = 0;                   // Para capturar o maior valor
// Variáveis para o Sensor de fluxo da água
float fluxo = 0;                              // Para calculo de fluxo da água
float volume = 0;                             // Para calculo do volume da água
float volume_total = 0;                       // Para calculo do volume total da água
unsigned long tempo_antes = 0;                // Para calcular o tempo de execução
unsigned long contador = 0;                   // Para calcular a o número de voltas
float densidade = 0;                          // Para calcular a densidade
unsigned long ultimoPulsoFluxo = 0;           // Marca o tempo do último pulso do sensor de fluxo
unsigned long tempoSemFluxo = 60000;          // Tempo máximo (em milissegundos) sem fluxo antes de zerar o volume_total

void incializarArray(int *array, int tam);
void atualizarArray(int *array, int valor, int tam);
void calcularMedia();
void maiorValor();
void printArray(int *array, int tam);
void exibirMonitor();
void enviarBluetooth();
void sensorFluxo();
float retornaPeso();


// DEFINIÇÕES DO BLUETOOTH
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run make menuconfig to and enable it
#endif

// Verifique o perfil da porta serial
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif

// Função de interrupção (executada em cada pulso)
void IRAM_ATTR contarPulsoEtanol() {
  unsigned long tempoAtualEtanol = micros();      // Tempo atual em microssegundos
  if (ultimoPulso > 0) {                          // Garante que não é o primeiro pulso
    tempoEntrePulsos = tempoAtualEtanol - ultimoPulso;  // Calcula o intervalo
    frequencia = 1000000.0 / tempoEntrePulsos;    // Calcula a frequência (Hz)
  }
  ultimoPulso = tempoAtualEtanol;                 // Atualiza o tempo do último pulso
} // END contarPulso

// Conta quantas voltas o sensor de fluxo fez
void IRAM_ATTR contarPulsoFluxo() {
  contador++;
  ultimoPulsoFluxo = millis();          // Atualiza o tempo do último pulso
} // END contarPulsoFluxo

// PRINCIPAL
void setup() {
  Serial.begin(115200);                           // Inicializa a comunicação serial
  SerialBT.begin("ESP32_SVAC");                   // Inicializa a comunicação do bluetooth
  Serial.println("O dispositivo \"ESP32_SVAC\" foi inciado.\nAguardando conexões...");
  scale.set_scale(fatorDeCalibracao);             // Define o fator de calibração
  scale.tare();                                   // Tara
  pinMode(SENSOR_ETANOL, INPUT);                  // Define o pino do sensor de ETANOL como entrada
  pinMode(PINO_FLUXO, INPUT_PULLUP);              // Define o pino do sensor de FLUXO como entrada pullup
  attachInterrupt(digitalPinToInterrupt(SENSOR_ETANOL), contarPulsoEtanol, FALLING);         // Configura a interrupção externa do sensor de etanol
  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), contarPulsoFluxo, FALLING);   // Configura a interrupção externa do sensor de fluxo
  incializarArray(leiturasE, MAX);                // Inicializando o array
  // Nada haver
  Serial.println("-------------------");
  Serial.println("| PROJETO S.V.A.C |");
  Serial.println("-------------------");
  // END Nada haver
} // END setup

void loop() {
  char comando = Serial.read();
  if (comando == 't') {
    scale.tare(5);
  }
  unsigned long tempoAtual = millis();                // Tempo atual desde que o arduino ligou
  sensorFluxo();                                      // Faz o calculo do sensor de fluxo
  porcentagemEtanol = map(frequencia, minFrequencia, maxFrequencia, 0, 100);  // Calculando a porcentagem de etanol
  porcentagemEtanol = constrain(porcentagemEtanol, 0, 100);                   // Limita entre 0 e 100%
  porcentagemGasolina = 100 - porcentagemEtanol;      // Calculando a porcentagem de gasolina
  atualizarArray(leiturasE, porcentagemEtanol, MAX);  // Atualizando o array com os novos valores
  calcularMedia();
  maiorValor();
  exibirMonitor();                                    // Exibindo os dados no sensor serial
  if (volume_total > 0) {
    densidade = peso / volume_total;                  // Calcula a densidade da gasolina identificada
  } else {
  densidade = 0;                                      // Define densidade como zero para evitar divisão por zero.
  }
  if (SerialBT.connected()) {
    enviarBluetooth();                                // Enviando os mesmos dados por BLUETOOTH
  } else {
    Serial.println("O bluetooth NÃO ESTÁ conectado.");
    Serial.println("----------------------------------------------------");
  } // END if
  delay(1000);
} // END loop

// FUNÇÕES ARRAY

// Função para inicializar o ARRAY
void incializarArray(int *array, int tam) {
  for (int i = 0; i < tam; i++) {
    array[i] = 0;  // Atribui zero para cada elemento do array
  }
} // END inicilizarArray

// Função para atualizar o array
void atualizarArray(int *array, int valor, int tam) {
  // desloca todos os elementos para a esquerda
  for (int i = 0; i < (tam-1); i++) {
    array[i] = array[i + 1];
    }
  array[tam - 1] = valor;  // Adiciona o novo valor na última posição do array
} // END atualizaArray

// Função para calcular a média
void calcularMedia() {
  int contadorEtanol = 0;                     // Reinicializa a variável localmente
  long somaTotal = 0;                         // Reinicializa a soma total localmente para evitar acumulação
  for (int i = 0; i < MAX; i++) {
    if ((leiturasE[i] > 0) && (leiturasE[i] != 100)) {
        contadorEtanol++;
        somaTotal += leiturasE[i];
    }
  }
  if (contadorEtanol != 0) {
    media = somaTotal / contadorEtanol;
    } else {
    media = 0;                                // Define média como 0 se nenhuma leitura válida for encontrada
    }
}// END calculaMedia

// Função para encontrar o maior valor
void maiorValor() {
  for (int i = 1; i < MAX; i++) {
    if ((leiturasE[i] > maiorValorEtanol) && (leiturasE[i] != 100)) {
      maiorValorEtanol = leiturasE[i];
    }
  }
}// END maiorValor

// Exbir Array
void printArray(int *array, int tam) {
  for (int i = 0; i < tam; i++) {
    Serial.print(array[i]);
    if (i < tam - 1) {
      Serial.print(", ");
    }
  }
  Serial.println();
} // END printArray

// FUNÇÕES MONITOR SERIAL

// Exibir no monitor serial
void exibirMonitor() {
    if (porcentagemEtanol > 0) {
    // Exibi no monitor serial do ESP32 o per. de ETHANOL
    Serial.print("ETHANOL = ");
    Serial.print(porcentagemEtanol);
    Serial.println("%");
  } else {
    // Exibi no monitor serial do ESP32
    Serial.print("ETHANOL = ");
    Serial.println("NO FUEL");
  }

  // Calcula e exibe a média dos últimos valores
  Serial.print("Média ETHANOL = ");
  Serial.print(media);
  Serial.println("%");

  // Calcula e exibe o maior valor
  Serial.print("Maior per. de ETHANOL = ");
  Serial.print(maiorValorEtanol);
  Serial.println("%");

  // Exibe o volume total de água
  Serial.print("Volume de gasolina: ");
  Serial.print(volume_total);             // Exibe o volume calculado pela função sensorFluxo() *Variável global
  Serial.println(" L");

  // Exibe a densidade da gasolina
  Serial.print("Densidade: ");
  Serial.print(densidade);
  Serial.println(" g/L");

  // Exbindo o array das leituras do sensor de etanol
  Serial.println("Exibindo o array das Leituras (ETHANOL): ");
  printArray(leiturasE, MAX);
  Serial.println("----------------------------------------------------");
  float pesoAtual = retornaPeso();
  Serial.print("Peso: ");
  Serial.print(pesoAtual, 3);               // Exibe o peso da celula de carga em gramas * Variável global
  Serial.println(" Kg");
  Serial.println("----------------------------------------------------");
} // END exibirMonitor

// FUNÇÕES BLUETOOTH

// Enviar Bluetooth
void enviarBluetooth() {  
    if(porcentagemEtanol > 0) 
    {
        String dados = "litros:" + String(volume_total, 1) +
                       ",etanol:" + String(media, 1) +
                       ",densidade:" + String(densidade, 1);
        SerialBT.println(dados);
    } 
    else 
    {
        SerialBT.println("Sem dados de fluxo ou peso.");
    }
} // END enviarBlutooth

// Função para calcular o volume da agua
void sensorFluxo() {
  if ((millis() - tempo_antes) > 1000) {
    detachInterrupt(PINO_FLUXO);                // Desabilita a interrupção para os calculos
    fluxo = ((1000.0 / (millis() - tempo_antes)) * contador) / CALIBRACAO_FLUXO; // Conversao do valor de pulsos para L/min
    volume = fluxo / 60.0;                      // Calcula o volume
    volume_total += volume;                     // Calcula o volume total
    contador = 0;                               // Reinicializa o contador pulse
    tempo_antes = millis();                     // Atualiza a variável de tempo
    attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), contarPulsoFluxo, FALLING);
  }
} // END sensorFluxo

// Função para calcula o peso
float retornaPeso() {
  peso = scale.get_units(5);                    // Captura o peso da celula de carga
  if (maiorPeso < peso) {
    maiorPeso = peso;
  }
  // if(peso < 0){                                 // Tara se o valor for menor que zero
  //   peso = 0;
  //   scale.tare();
  //   Serial.println("Menor que zero");
  //   delay(100);
  // }
  // float pesoEmGramas = peso * 1000;               // Converter para gramas
  // int pesoEmGramasINT = (int)pesoEmGramas;        // Converter para inteiro
  return (peso);
}// END retornaPeso