Nume: Roman Bogdan-Gabriel
Grupa: 332CB
TEMA 1

=== Organizare ===

Instructiunile ce trebuiau implementate in biblioteca se executa dupa 
urmatorul plan:
	1) Instructiunea propriu-zisa este executata

	2) Se verifica timpul pe care thread-ul curent il mai are disponibil pe
	procesor si in functie de aceasta valoare thread-ul isi va continua
	work-flow-ul sau va fi preemptat

	2_aux) In cazul in care un nou thread intra in sistem se verifica daca
	acest nou thread are prioritate mai mare si in caz afirmativ, thread-ul
	curent este preemptat

	3) In urma deciziilor luate la subpunctele anterioare, scheduler-ul
	programeaza urmatorul thread pe procesor

=== Implementare ===

In continuare voi detalia implementarea functiei ce gestioneaza
organizarea thread-urilor pe procesor:

check_scheduler(int action) {
	
	switch(action)
		case TIME_EXPIRED || NEW_SYSTEM_THREAD {

			Find greater priority thread;
			Extract the greater priority thread from the queue;
			Mark it as the current running thread;
			Introduce the previous running thread into the queue;
			Signal the new thread that it car start running again;
			Make the old thread wait until it is re-scheduled
		}

		case TERMINATED {
			Find greater priority thread;
			Extract the greater priority thread from the queue;
			Mark it as the current running thread;
			Signal the new thread that it car start running again;
		}
}

Basically, fiecare thread va avea semaforul sau propriu, pe care va face wait
ori de cate ori aceste trebuie preempatat. Cand un thread trebuie sa iasa din
sistem sau este preemptat, acesta va da un semnal sem_post thread-ului cu cea
mai mare prioritate, el se va bloca in asteptare sem_wait() si thread-ul cu
prioritatea mai mare isi va relua work flow-ul de unde ramasese.

=== Compilare si Rulare ===

Pentru obtinerea bibliotecii:
	make libscheduler.so