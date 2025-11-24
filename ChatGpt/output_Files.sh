#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------------------------------------------
# output_Files.sh
#   Lit le contenu des fichiers provenant de dossiers et/ou fichiers passés
#   en paramètres, filtrés par extensions, en excluant certains dossiers.
#
#   Options:
#     -d, --directory  (répétable) : liste de chemins (dossiers et/ou fichiers)
#     -e, --extensions (répétable) : extensions à inclure (ex: .md .cpp)
#     -x, --exclude    (répétable) : motifs de dossiers à exclure (substring)
#     -h, --help                     : aide
#
#   Exemple:
#     ./output_Files.sh \
#       -d "../A7/prg/network" "../A7/prg/dataProcessor" \
#       -d "./Chatgpt_instruction.md" "../A7/prg/main.cpp" "../A7/Azure/Dockerfile" \
#       -e .md .txt .cpp .c .h .hpp .html .css .js .ini \
#       -x "node_modules" "build"
# ------------------------------------------------------------------------------

print_usage() {
  cat <<'USAGE'
Usage: output_Files.sh -d <path...> -e <ext...> [-x <pattern...>]

Options:
  -d, --directory   Chemins (dossiers et/ou fichiers). Option répétable.
  -e, --extensions  Extensions à inclure (avec ou sans point). Option répétable.
  -x, --exclude     Motifs de dossiers à exclure (substring). Option répétable.
  -h, --help        Affiche cette aide.

Notes:
- Répète -d/-e/-x autant de fois que nécessaire; ou mets plusieurs valeurs après l'option.
- Les extensions sont comparées sans tenir compte de la casse. ".md" et "MD" sont équivalents.
- L'exclusion s'applique si le chemin du dossier CONTIENT l'un des motifs donnés.
USAGE
}

# --- Parsing des arguments -----------------------------------------------------
directories=()
extensions=()
exclude_patterns=()

# Parse manuel pour gérer "plusieurs valeurs" après une option
if [[ $# -eq 0 ]]; then
  print_usage
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--directory)
      shift
      # Consomme toutes les valeurs jusqu'à la prochaine option (qui commence par '-')
      while [[ $# -gt 0 && "$1" != -* ]]; do
        directories+=("$1")
        shift
      done
      ;;
    -e|--extensions)
      shift
      while [[ $# -gt 0 && "$1" != -* ]]; do
        extensions+=("$1")
        shift
      done
      ;;
    -x|--exclude)
      shift
      while [[ $# -gt 0 && "$1" != -* ]]; do
        exclude_patterns+=("$1")
        shift
      done
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    --) # fin explicite des options
      shift
      break
      ;;
    *)
      echo "Option inconnue: $1" >&2
      print_usage
      exit 1
      ;;
  esac
done

# --- Validations similaires à ValidateScript/param Mandatory ------------------
if [[ ${#directories[@]} -eq 0 ]]; then
  echo "Erreur: au moins un chemin doit être fourni via -d/--directory." >&2
  exit 1
fi
if [[ ${#extensions[@]} -eq 0 ]]; then
  echo "Erreur: au moins une extension doit être fournie via -e/--extensions." >&2
  exit 1
fi

# Vérifie l'existence de chaque chemin fourni
for p in "${directories[@]}"; do
  if [[ ! -e "$p" ]]; then
    echo "Path '$p' does not exist." >&2
    exit 1
  fi
done

# Normalise les extensions: minuscule + préfixées d'un point
norm_extensions=()
for ex in "${extensions[@]}"; do
  ex_lc="${ex,,}"
  if [[ "${ex_lc:0:1}" != "." ]]; then
    ex_lc=".$ex_lc"
  fi
  norm_extensions+=("$ex_lc")
done

# --- Fonctions ----------------------------------------------------------------
write_file_details() {
  # $1 = chemin du fichier
  local file="$1"
  local name path

  name="$(basename -- "$file")"
  # readlink -f pour chemin absolu; si indispo, fallback à 'realpath'
  if path="$(readlink -f -- "$file" 2>/dev/null)"; then
    :
  elif path="$(realpath -- "$file" 2>/dev/null)"; then
    :
  else
    # dernier recours: construit un chemin absolu approximatif
    path="$(cd -- "$(dirname -- "$file")" && pwd)/$(basename -- "$file")"
  fi

  printf "%s\n" "$name"
  printf "%s\n" "-----------------------------------------"
  printf "%s\n" "$path"
  printf "%s\n" "-----------------------------------------"

  if ! cat -- "$file" 2>/dev/null; then
    # message d'erreur similaire au catch PowerShell
    echo "Error reading file content: unable to read file."
  fi

  printf "%s\n" "========================"
}

has_allowed_extension() {
  # $1 = chemin du fichier
  local file="$1"
  local base ext_with_dot ext_lc
  base="$(basename -- "$file")"
  # Si pas de point ou fichier caché sans extension, ext vide
  if [[ "$base" == *.* && "$base" != .* ]]; then
    ext_with_dot=".${base##*.}"
  else
    ext_with_dot=""
  fi
  ext_lc="${ext_with_dot,,}"
  if [[ -z "$ext_lc" ]]; then
    return 1
  fi
  for e in "${norm_extensions[@]}"; do
    [[ "$ext_lc" == "$e" ]] && return 0
  done
  return 1
}

is_excluded_by_pattern() {
  # $1 = chemin du fichier
  local file="$1"
  local dir
  dir="$(dirname -- "$file")"
  for pat in "${exclude_patterns[@]}"; do
    [[ -n "$pat" && "$dir" == *"$pat"* ]] && return 0
  done
  return 1
}

# --- Collecte et filtrage des fichiers ---------------------------------------
all_files=()

for path in "${directories[@]}"; do
  if [[ -d "$path" ]]; then
    # Liste récursive des fichiers du dossier
    while IFS= read -r -d '' f; do
      # Exclusion par motif sur le dossier
      if is_excluded_by_pattern "$f"; then
        continue
      fi
      # Filtre par extension
      if has_allowed_extension "$f"; then
        all_files+=("$f")
      fi
    done < <(find "$path" -type f -print0)
  elif [[ -f "$path" ]]; then
    # Fichier individuel: ne filtre pas par extension ici pour coller au comportement PS?
    # Dans le script PowerShell, les fichiers individuels sont ajoutés sans filtre d'extension.
    all_files+=("$path")
  fi
done

# --- Sortie -------------------------------------------------------------------
for f in "${all_files[@]}"; do
  write_file_details "$f"
done
