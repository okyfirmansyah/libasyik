#!/usr/bin/env bash
# Regenerates the throwaway PKI used by the TLS test suite.
#
# Output (all PEM, committed to the repo):
#   ca.crt                         trusted test root CA
#   server.crt / server.key        localhost, 127.0.0.1, ::1   (signed by ca)
#   server2.crt / server2.key      same names, different key   (signed by ca)
#   wronghost.crt / wronghost.key  wrong.example.com only      (signed by ca)
#   expired.crt / expired.key      localhost, 127.0.0.1, expired in 2001
#   untrusted.crt / untrusted.key  localhost, 127.0.0.1, signed by an unknown CA
#   client.crt / client.key        TLS client certificate      (signed by ca)
#   client_encrypted.key           client.key encrypted with "asyik-test"
#
# Nothing here is secret: these keys exist only for tests.
set -euo pipefail

OUT="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

DAYS=7300
CURVE="-newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes"

make_ca() {  # name, subject
  openssl req -x509 $CURVE -keyout "$1.key" -out "$1.crt" -days $DAYS \
    -subj "$2" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
}

make_csr() {  # name, subject
  openssl req -new $CURVE -keyout "$1.key" -out "$1.csr" -subj "$2" 2>/dev/null
}

ext_file() {  # name, san, eku
  cat > "$1.ext" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=$3
subjectAltName=$2
EOF
}

sign() {  # name, ca, san, eku
  ext_file "$1" "$3" "$4"
  openssl x509 -req -in "$1.csr" -CA "$2.crt" -CAkey "$2.key" \
    -CAcreateserial -days $DAYS -sha256 -extfile "$1.ext" -out "$1.crt" \
    2>/dev/null
}

LOCAL_SAN="DNS:localhost,IP:127.0.0.1,IP:::1"

make_ca ca "/CN=libasyik Test Root CA"
make_ca rogue "/CN=libasyik Untrusted Test CA"

make_csr server "/CN=localhost"
sign server ca "$LOCAL_SAN" serverAuth

make_csr server2 "/CN=localhost"
sign server2 ca "$LOCAL_SAN" serverAuth

make_csr wronghost "/CN=wrong.example.com"
sign wronghost ca "DNS:wrong.example.com" serverAuth

make_csr untrusted "/CN=localhost"
sign untrusted rogue "$LOCAL_SAN" serverAuth

make_csr client "/CN=libasyik test client"
sign client ca "DNS:libasyik-test-client" clientAuth

openssl pkey -in client.key -aes256 -passout pass:asyik-test \
  -out client_encrypted.key

# `openssl x509` cannot back-date certificates, so use a minimal `openssl ca`.
make_csr expired "/CN=localhost"
ext_file expired "$LOCAL_SAN" serverAuth
mkdir -p ca_db && touch ca_db/index.txt && echo 1000 > ca_db/serial
cat > ca.cnf <<EOF
[ca]
default_ca = test_ca
[test_ca]
database = ca_db/index.txt
serial = ca_db/serial
new_certs_dir = ca_db
default_md = sha256
policy = any
unique_subject = no
[any]
commonName = supplied
EOF
openssl ca -batch -config ca.cnf -cert ca.crt -keyfile ca.key \
  -in expired.csr -out expired.crt -extfile expired.ext \
  -startdate 20000101000000Z -enddate 20010101000000Z -notext 2>/dev/null

for f in ca.crt server.crt server.key server2.crt server2.key \
         wronghost.crt wronghost.key expired.crt expired.key \
         untrusted.crt untrusted.key client.crt client.key \
         client_encrypted.key; do
  cp "$f" "$OUT/$f"
done

echo "test certificates written to $OUT"
