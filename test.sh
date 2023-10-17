#!/bin/sh
set -e

function cleanup {
  rm  "$tmp"
}
tmp=$(mktemp)
trap cleanup EXIT

echo verify all index files
for f in /usr/local/freedict/*; do
	b=$(basename $f);
	echo -n .
	./obj/dict -D $b -m a >/dev/null
done
echo

echo lookup every word
for f in /usr/local/freedict/*; do
	b=$(basename "$f");
	echo -n .
	cut -d'	' -f1 "$f/$b.index" | grep -v '^$' | uniq > "$tmp"
	idx=$(cat "$tmp" | wc -l)
	dct=$(cat "$tmp" | tr \\n \\0 | xargs -0 -n 900 ./obj/dict -VeD "$b" | wc -l)
	if [ "$idx" -ne "$dct" ]; then
		cat "$tmp"
		echo "$b: $idx vs $dct"
		exit 1
	fi
done
echo

echo lookup between every word
for f in /usr/local/freedict/*; do
	b=$(basename "$f");
	f=/usr/local/freedict/fra-deu
	b=fra-deu
	echo -n .
	echo ! > "$tmp"
	cut -d'	' -f1 "$f/$b.index" | grep -v '^$' | uniq | sed -e 's/$/!/g' >> "$tmp"
	dct=$(cat "$tmp" | tr \\n \\0 | xargs -0 -n 900 ./obj/dict -VeD "$b")
	if [ -n "$dct" ]; then
		cat "$tmp"
		echo "$b: $dct"
		exit 1
	fi
done
echo
