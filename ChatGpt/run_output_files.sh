#!/usr/bin/env bash
set -euo pipefail

# --- Config: répertoires, fichiers et extensions (chemins Linux) ---
dirs=(
  "../src"
  "../data"
)

files=(
  "./Chatgpt_instruction.md"
  "../platformio.ini"
)

ext=(".md" ".txt" ".cpp" ".c" ".h" ".hpp" ".html" ".css" ".js" ".ini")

# --- Localisation du script Bash ---
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
SCRIPT_SH="${SCRIPT_DIR}/output_Files.sh"

if [[ ! -f "$SCRIPT_SH" ]]; then
  echo "❌ Le fichier output_Files.sh est introuvable dans le dossier du script." >&2
  exit 1
fi

echo "⏳ Exécution du script output_Files.sh..."
output="$(
  "$SCRIPT_SH" \
    -d "${dirs[@]}" "${files[@]}" \
    -e "${ext[@]}"
)"

copy_to_clipboard() {
  # 1) Si on est dans WSL → utiliser le presse-papiers Windows
  if grep -qi microsoft /proc/version 2>/dev/null || [[ -n "${WSL_DISTRO_NAME-}" ]]; then
    # Envoi au presse-papiers Windows
    printf "%s" "$output" | clip.exe
    return 0
  fi

  # 2) Linux natif : Wayland / X11
  if command -v wl-copy >/dev/null 2>&1; then
    printf "%s" "$output" | wl-copy
    return 0
  elif command -v xclip >/dev/null 2>&1; then
    # CLIPBOARD = Ctrl+C/V ; PRIMARY = clic milieu
    printf "%s" "$output" | xclip -selection clipboard
    printf "%s" "$output" | xclip -selection primary
    return 0
  elif command -v xsel >/dev/null 2>&1; then
    printf "%s" "$output" | xsel --clipboard --input
    printf "%s" "$output" | xsel --primary   --input
    return 0
  fi

  return 1
}

if copy_to_clipboard; then
  echo "✅ Les fichiers ont été copiés dans le presse-papiers."
else
  echo "⚠️ Aucun utilitaire de presse-papiers détecté. Contenu ci-dessous :"
  echo "------------------------------------------------------------"
  printf "%s\n" "$output"
  echo "------------------------------------------------------------"
fi
