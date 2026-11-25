#!/bin/sh
# Re-extract translatable strings into po/ksipphone.pot and merge them into every po/<lang>/ksipphone.po.
set -e
cd "$(dirname "$0")/.."
xgettext --from-code=UTF-8 -C --kde -ci18n -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 -kki18n:1 -kki18nc:1c,2 \
    --package-name=ksipphone --msgid-bugs-address=https://github.com/gongsearch/ksipphone/issues \
    -o po/ksipphone.pot src/*.cpp src/*.h
for po in po/*/ksipphone.po; do
    msgmerge --update --backup=none --no-fuzzy-matching "$po" po/ksipphone.pot
    printf '%s: ' "$po"; msgfmt --check --statistics -o /dev/null "$po"
done
