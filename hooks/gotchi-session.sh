#!/bin/sh
# Elige qué sesión de Claude Code sigue el gotchi.
#   gotchi-session.sh            lista las sesiones activas (numeradas)
#   gotchi-session.sh use <n|id|nombre>   sigue esa sesión (n = número de la lista)
#   gotchi-session.sh auto       sigue todas (automático)
# URL configurable con GOTCHI_URL (por defecto producción).
URL="${GOTCHI_URL:-https://atom-api.northernarchive.com}"

list() {
  curl -s -m 5 "$URL/api/sessions" | python3 -c "$(cat <<'PY'
import json, sys
d = json.load(sys.stdin)
print("seguida: " + (d["active"] or "auto (todas)"))
for i, s in enumerate(d["sessions"], 1):
    mark = "*" if s["active"] else " "
    st = s["state"] + (("/" + s["tool"]) if s["tool"] else "")
    print(f"{mark} {i}. {s['name']:<22} {st:<12} hechas={s['doneCount']} inactiva={s['idleSeconds']}s  id={s['id'][:8]}")
PY
)"
}

case "$1" in
  ""|list) list ;;
  auto) curl -s -m 5 -X POST -H 'Content-Type: application/json' -d '{"id":"auto"}' "$URL/api/active"; echo ;;
  use)
    target="$2"
    case "$target" in
      ''|*[!0-9]*) ;;   # no es un número: se envía tal cual (id, prefijo o nombre)
      *) target=$(curl -s -m 5 "$URL/api/sessions" | python3 -c 'import json,sys; d=json.load(sys.stdin)["sessions"]; n=int(sys.argv[1]); print(d[n-1]["id"] if 0<n<=len(d) else "")' "$target") ;;
    esac
    [ -z "$target" ] && { echo "sesión no encontrada" >&2; exit 1; }
    curl -s -m 5 -X POST -H 'Content-Type: application/json' -d "{\"id\":\"$target\"}" "$URL/api/active"; echo ;;
  *) echo "uso: $0 [list | use <n|id|nombre> | auto]" >&2; exit 1 ;;
esac
