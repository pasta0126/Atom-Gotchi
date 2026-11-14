#!/bin/sh
# Hook de Claude Code: reenvía el JSON del evento al servidor del Gotchi sin bloquear a Claude.
# URL configurable con GOTCHI_URL (por defecto el servidor local de desarrollo).
payload=$(cat)
KEY=$(cat "${GOTCHI_KEY_FILE:-$HOME/.config/atom-gotchi/key}" 2>/dev/null)
printf '%s' "$payload" | curl -s -m 1 -X POST -H 'Content-Type: application/json' -H "X-Api-Key: $KEY" \
  --data-binary @- "${GOTCHI_URL:-https://atom-api.northernarchive.com}/api/hook" >/dev/null 2>&1 &
exit 0
