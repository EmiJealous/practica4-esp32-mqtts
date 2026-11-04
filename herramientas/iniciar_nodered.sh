#!/usr/bin/env bash
# Arranca Node-RED con la clave AES como variable de entorno.
# La clave sale de .secrets/credenciales.env (nunca se sube al repo) y el nodo
# "Descifrar AES-GCM" la lee con env.get("AES_KEY_HEX"), asi no queda guardada en el flujo.
set -euo pipefail
cd "$(dirname "$0")/.."

AES_KEY_HEX=$(grep '^AES_KEY_HEX=' .secrets/credenciales.env | cut -d= -f2)
export AES_KEY_HEX

exec node-red "$@"
