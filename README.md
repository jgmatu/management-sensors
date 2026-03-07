# Botan Hybrid TLS Test Server

This simple web app can be used to test your client against Botan's TLS
implementation. The server itself is using Boost asio for TCP, [Botan's asio
stream](https://botan.randombit.net/handbook/api_ref/tls.html#tls-stream) for
TLS and Boost beast for HTTP.

The app displays a web page inspired by [Cloudflare's post-quantum TLS test
page](https://pq.cloudflareresearch.com), that visualizes what key exchange
algorithm was used to establish the TLS connection from the user's browser and
the test server.

## Online Test

A running version of the app is deployed here:
[https://pq.botan-crypto.org](https://pq.botan-crypto.org).

## Build

At the moment, you'll need the latest (i.e. unreleased) revision of Botan.
Therefore, this repository contains it as a submodule. As soon as Botan 3.3.0 is
released (expected in January 2024), we'll remove the submodule and will rely on
Conan for the dependencies.

Additionally, a fairly recent version of Boost is required. The steps below
assume that it is available on your system.

## Co routines C++20

``` bash
https://lewissbaker.github.io/
```

## Dependencies

```bash
Install boost and botan libraries from sources:
  - https://www.boost.org/
  - https://botan.randombit.net/
```

## Run

You'll need a certificate for your test server. If you don't have one, you can
create a basic certificate with Botan's CLI tool. After building Botan as
described above, from the repository's root run:

```bash
mkdir certs

botan keygen --algo=ECDSA --params=secp384r1 --output=certs/ca.key
botan keygen --algo=ECDSA --params=secp384r1 --output=certs/server.key
botan gen_self_signed --ca --country=DE --dns=localhost --hash=SHA-384 --output=certs/ca.pem certs/ca.key localhost
botan gen_pkcs10 --output=certs/server.req certs/server.key localhost
botan sign_cert --output=certs/server.pem certs/ca.pem certs/ca.key certs/server.req
```

## Example echo TLS v1.3 echo client
```bash
$ botan tls_client localhost --port=50443 --policy=./policies/client_policies.txt --trusted-cas=certs/
Certificate validation status: Verified
Handshake complete, TLS v1.3
Negotiated ciphersuite CHACHA20_POLY1305_SHA256
Key exchange using ML-KEM-768
Session ID 6998D4160565DE617358FC56EEF2227127931BA0CD8B4A93103B1DF6B39FDD9D
Handshake complete

```

## Example echo TLS v1.3 echo server
```bash
build/testserver --cert certs/server.pem --key certs/server.key --port 50443 --policy policies/pqc_basic.txt
```

Using the browser of you choice, visit: [https://localhost:50443](https://localhost:50443) to see it in action.
Using the browser of you choice, visit: [https://javi.es:50443](https://localhost:50443) to see it in action. (CERTS)

## PostgreSQL example pipeline output stream
```
Waiting for notifications... (Run 'NOTIFY events, 'hello';' in psql)
Received notification on channel: events
Payload: {"id": 71, "op": "UPDATE", "name": "cafe", "stock": 7432}
Received notification on channel: "events": 
{
  "channel" : "events",
  "payload" : {
    "id" : 71,
    "op" : "UPDATE",
    "name" : "cafe",
    "stock" : 7432
  }
}
Waiting for notifications... (Run 'NOTIFY events, 'hello';' in psql)
Received notification on channel: events
Payload: {"id": 71, "op": "UPDATE", "name": "cafe", "stock": 7433}
Received notification on channel: "events": 
{
  "channel" : "events",
  "payload" : {
    "id" : 71,
    "op" : "UPDATE",
    "name" : "cafe",
    "stock" : 7433
  }
}
Waiting for notifications... (Run 'NOTIFY events, 'hello';' in psql)
Received notification on channel: events
Payload: {"id": 71, "op": "UPDATE", "name": "cafe", "stock": 7434}
Received notification on channel: "events": 
{
  "channel" : "events",
  "payload" : {
    "id" : 71,
    "op" : "UPDATE",
    "name" : "cafe",
    "stock" : 7434
  }
}
Waiting for notifications... (Run 'NOTIFY events, 'hello';' in psql)
Received notification on channel: events
Payload: {"id": 71, "op": "UPDATE", "name": "cafe", "stock": 7435}
Received notification on channel: "events": 
{
  "channel" : "events",
  "payload" : {
    "id" : 71,
    "op" : "UPDATE",
    "name" : "cafe",
    "stock" : 7435
  }
}
Waiting for notifications... (Run 'NOTIFY events, 'hello';' in psql)
```
