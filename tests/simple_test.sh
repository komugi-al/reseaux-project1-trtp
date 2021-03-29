#!/bin/bash

# cleanup d'un test précédent
rm -f received_file input_file

# Fichier au contenu aléatoire de 512 octets
dd if=/dev/urandom of=input_file bs=1 count=$1 &> /dev/null

# On lance le simulateur de lien avec 10% de pertes et un délais de 50ms
valgrind_sender=""
valgrind_receiver=""
if [ ! -z "$VALGRIND" ] ; then
	valgrind_sender="valgrind -s --leak-check=full --log-file=valgrind_sender.log"
	valgrind_receiver="valgrind -s --leak-check=full --log-file=valgrind_receiver.log"
fi

./link_sim -p 1341 -P 2456 -l $2 -d $3 -e $4 -R  &> link.log &
link_pid=$!

# On lance le receiver et capture sa sortie standard
$valgrind_receiver ./receiver -s receiver.csv :: 2456 > received_file  2> receiver.log &
receiver_pid=$!

cleanup()
{
    kill -9 $receiver_pid
    kill -9 $link_pid
    exit 0
}
trap cleanup SIGINT  # Kill les process en arrière plan en cas de ^-C

# On démarre le transfert
if ! $valgrind_sender ./sender -s sender.csv ::1 1341 < input_file 2> sender.log ; then
  echo "Crash du sender!"
  cat sender.log
  err=1  # On enregistre l'erreur
fi

sleep 5 # On attend 5 seconde que le receiver finisse

if kill -0 $receiver_pid &> /dev/null ; then
  echo "Le receiver ne s'est pas arreté à la fin du transfert!"
  kill -9 $receiver_pid
  err=1
else  # On teste la valeur de retour du receiver
  if ! wait $receiver_pid ; then
    echo "Crash du receiver!"
    cat receiver.log
    err=1
  fi
fi

sleep 3

# On arrête le simulateur de lien
kill -9 $link_pid &> /dev/null

kill -9 $sender_pid &> /dev/null

# On vérifie que le transfert s'est bien déroulé
if [[ "$(md5sum input_file | awk '{print $1}')" != "$(md5sum received_file | awk '{print $1}')" ]]; then
  echo "Le transfert a corrompu le fichier!"
  echo "Diff binaire des deux fichiers: (attendu vs produit)"
  diff -C 9 <(od -Ax -t x1z input_file) <(od -Ax -t x1z received_file)
  exit 1
else
  echo "Le transfert est réussi!"
  exit ${err:-0}  # En cas d'erreurs avant, on renvoie le code d'erreur
fi
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 968maxresident)k
0inputs+0outputs (0major+24minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 924maxresident)k
0inputs+0outputs (0major+23minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 964maxresident)k
0inputs+0outputs (0major+24minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 1048maxresident)k
0inputs+0outputs (0major+26minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 968maxresident)k
0inputs+0outputs (0major+24minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 1064maxresident)k
0inputs+0outputs (0major+26minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 952maxresident)k
0inputs+0outputs (0major+24minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 1048maxresident)k
0inputs+0outputs (0major+25minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 1064maxresident)k
0inputs+0outputs (0major+26minor)pagefaults 0swaps
Command exited with non-zero status 127
0.00user 0.00system 0:00.00elapsed ?%CPU (0avgtext+0avgdata 952maxresident)k
0inputs+0outputs (0major+24minor)pagefaults 0swaps
