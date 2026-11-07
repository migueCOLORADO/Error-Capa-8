#!/usr/bin/env bash
# Versiona la Parte I (analisis conceptual) en el README. Uso: bash actualizarParte1.sh dry-run|run
set -euo pipefail

MODO="${1:-}"
if [[ "$MODO" != "dry-run" && "$MODO" != "run" ]]; then
  echo "Uso: bash actualizarParte1.sh dry-run|run" >&2
  exit 1
fi

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${REPO:-$RAIZ/Error-Capa-8}"
RAMA="${RAMA:-main}"
MSG="docs(readme): agregar Parte I analisis conceptual"
ARCHIVOS=("README.md" "Parte1_Analisis_Conceptual.md")

git -C "$REPO" rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "No es un repo git: $REPO" >&2; exit 1; }
ACTUAL="$(git -C "$REPO" branch --show-current)"
[[ "$ACTUAL" == "$RAMA" ]] || { echo "Rama actual '$ACTUAL', se esperaba '$RAMA'" >&2; exit 1; }

# Solo los archivos de la lista que tengan cambios
CAMBIOS=()
for f in "${ARCHIVOS[@]}"; do
  if [[ -n "$(git -C "$REPO" status --porcelain -- "$f")" ]]; then
    CAMBIOS+=("$f")
  fi
done

echo "Repo: $REPO ($RAMA)"
echo "Modo: $MODO"
echo "Mensaje: $MSG"

if [[ ${#CAMBIOS[@]} -eq 0 ]]; then
  echo "Sin cambios en: ${ARCHIVOS[*]}. Nada que versionar."
  exit 0
fi

echo "Archivos a versionar: ${CAMBIOS[*]}"
echo "--- git status --short"
git -C "$REPO" status --short -- "${CAMBIOS[@]}"
echo "--- git diff --stat"
git -C "$REPO" diff --stat -- "${CAMBIOS[@]}"

if [[ "$MODO" == "dry-run" ]]; then
  echo "--- git push --dry-run origin $RAMA"
  git -C "$REPO" push --dry-run origin "$RAMA"
  echo "dry-run terminado: no se modifico nada."
  exit 0
fi

git -C "$REPO" add -- "${CAMBIOS[@]}"
git -C "$REPO" commit -m "$MSG"
echo "--- git status"
git -C "$REPO" status --short
git -C "$REPO" push origin "$RAMA"
echo "Listo: commit y push de ${CAMBIOS[*]}"
