#!/bin/sh
# Hook de Claude Code: reenvía el JSON del evento al servidor del Gotchi sin bloquear a Claude.
# URL configurable con GOTCHI_URL (por defecto el servidor local de desarrollo).
payload=$(cat)
printf '%s' "$payload" | curl -s -m 1 -X POST -H 'Content-Type: application/json' \
  --data-binary @- "${GOTCHI_URL:-http://localhost:5055}/api/hook" >/dev/null 2>&1 &
exit 0
