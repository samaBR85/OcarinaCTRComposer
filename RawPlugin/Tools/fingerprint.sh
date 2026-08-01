#!/bin/sh
# Impressao digital do binario: nome + tamanho de cada funcao/simbolo definido, SEM enderecos.
# Se ela nao muda entre duas builds, o compilador gerou exatamente o mesmo codigo - util para
# provar que uma refatoracao puramente organizacional (ex: extrair uma secao do main.c para um
# arquivo incluido via #include) nao alterou o binario final.
#
# Uso:
#   sh Tools/fingerprint.sh CTRComposer-BlankTemplate.elf > /tmp/base.fp
#   ...faz a mudanca, recompila...
#   sh Tools/fingerprint.sh CTRComposer-BlankTemplate.elf > /tmp/depois.fp
#   diff /tmp/base.fp /tmp/depois.fp
#
# Requer o .elf, nao o .3gx - por isso o Makefile marca %.elf como .PRECIOUS (ver PLANO-
# REFATORACAO.md, Etapa 0), senao make apaga o .elf assim que o .3gx e criado.

set -eu

if [ $# -ne 1 ]; then
    echo "uso: $0 <arquivo.elf>" >&2
    exit 1
fi

ELF="$1"

if [ ! -f "$ELF" ]; then
    echo "arquivo nao encontrado: $ELF" >&2
    echo "dica: rode 'make' primeiro (o .elf so existe apos o link)" >&2
    exit 1
fi

# Tenta, em ordem: nm no PATH, o DEVKITARM do ambiente (msys2), o caminho padrao no Windows
# (git-bash, sem DEVKITARM setado), o caminho padrao no Linux/macOS.
NM=""
for candidate in \
    "arm-none-eabi-nm" \
    "${DEVKITARM:-}/bin/arm-none-eabi-nm" \
    "/c/devkitPro/devkitARM/bin/arm-none-eabi-nm.exe" \
    "/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm" \
;do
    [ -z "$candidate" ] && continue
    if command -v "$candidate" >/dev/null 2>&1; then NM="$candidate"; break; fi
done

if [ -z "$NM" ]; then
    echo "arm-none-eabi-nm nao encontrado. Defina DEVKITARM ou ajuste Tools/fingerprint.sh." >&2
    exit 1
fi

# O sed normaliza os sufixos numericos que o GCC gera (CSWTCH.925, foo.isra.0, bar.constprop.1):
# esses numeros sao arbitrarios e mudam so de reordenar o arquivo, sem nenhum codigo mudar. O
# TAMANHO continua sendo comparado, entao a checagem nao perde forca - so deixa de acusar ruido.
"$NM" --print-size --defined-only "$ELF" \
    | awk '{print $NF, $2}' \
    | sed -E 's/\.(CSWTCH|isra|constprop|part|cold|lto_priv)\.[0-9]+/.\1/g; s/^CSWTCH\.[0-9]+/CSWTCH/' \
    | sort
