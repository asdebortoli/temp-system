#!/usr/bin/env bash
#
# gen-certs.sh — gera a CA e o certificado TLS do broker Mosquitto local e o
# arquivo de senha. Copia a CA para main/certs/broker_ca.pem (o firmware valida
# o broker por ela). Rode uma vez antes de subir o broker e de buildar o firmware.
#
#   cd cloud/broker && ./gen-certs.sh
#
# Variáveis opcionais: MQTT_USER, MQTT_PASS (padrão tmt/tmt).
set -euo pipefail
cd "$(dirname "$0")"

CERTS=certs
DAYS=3650
MQTT_USER="${MQTT_USER:-tmt}"
MQTT_PASS="${MQTT_PASS:-tmt}"

mkdir -p "$CERTS"

echo ">> Gerando CA..."
openssl genrsa -out "$CERTS/ca.key" 2048
openssl req -x509 -new -nodes -key "$CERTS/ca.key" -sha256 -days "$DAYS" \
  -subj "/C=BR/O=TMT/CN=TMT-Local-CA" -out "$CERTS/ca.crt"

echo ">> Gerando certificado do servidor (assinado pela CA)..."
openssl genrsa -out "$CERTS/server.key" 2048
openssl req -new -key "$CERTS/server.key" \
  -subj "/C=BR/O=TMT/CN=tmt-broker" -out "$CERTS/server.csr"
openssl x509 -req -in "$CERTS/server.csr" \
  -CA "$CERTS/ca.crt" -CAkey "$CERTS/ca.key" -CAcreateserial \
  -days "$DAYS" -sha256 -out "$CERTS/server.crt"
rm -f "$CERTS/server.csr"

# O container roda como uid 1883 e precisa ler os arquivos montados (demo local).
chmod 644 "$CERTS"/*.crt "$CERTS"/*.key

echo ">> Embarcando a CA no firmware (main/certs/broker_ca.pem)..."
mkdir -p ../../main/certs
cp "$CERTS/ca.crt" ../../main/certs/broker_ca.pem

echo ">> Gerando arquivo de senha do Mosquitto (usuário=$MQTT_USER)..."
docker run --rm eclipse-mosquitto:2 \
  sh -c "mosquitto_passwd -b -c /tmp/pw '$MQTT_USER' '$MQTT_PASS' >/dev/null && cat /tmp/pw" > passwd
chmod 644 passwd

echo ""
echo "OK. Certificados em $CERTS/, CA em main/certs/broker_ca.pem, senha em ./passwd"
echo "Suba o broker com: docker compose up -d"
