#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

// Définition des rôles
typedef enum { USER, ADMIN } Role;

// Structure pour les VMS
typedef struct VM {
    int id;
    char nom_executable[256];
    pid_t pid; // Identifiant du processus
    struct VM* suivant;
} VM;

// Structure pour les Transactions
typedef struct Transaction {
    char type;
    int param1;
    int param2;
    char executable[256];
} Transaction;

// Mutex pour la synchronisation
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

VM* tete = NULL;

// Rôle de l'utilisateur
Role user_role;

// Fonction pour ajouter une VM à la liste
void ajouter_vm(int id, const char* executable, pid_t pid) {
    pthread_mutex_lock(&mutex);
    VM* nouvelle_vm = (VM*)malloc(sizeof(VM));
    if (!nouvelle_vm) {
        perror("Erreur d'allocation mémoire");
        pthread_mutex_unlock(&mutex);
        return;
    }
    nouvelle_vm->id = id;
    strcpy(nouvelle_vm->nom_executable, executable);
    nouvelle_vm->pid = pid;
    nouvelle_vm->suivant = tete;
    tete = nouvelle_vm;
    pthread_mutex_unlock(&mutex);
}

// Fonction pour supprimer une VM de la liste
void supprimer_vm(int id) {
    pthread_mutex_lock(&mutex);
    VM* courant = tete;
    VM* precedent = NULL;
    while (courant != NULL && courant->id != id) {
        precedent = courant;
        courant = courant->suivant;
    }
    if (courant != NULL) {
        if (precedent == NULL) {
            tete = courant->suivant;
        } else {
            precedent->suivant = courant->suivant;
        }
        // Terminer le processus associé
        if (kill(courant->pid, SIGTERM) == -1) {
            perror("Erreur lors de l'envoi du signal SIGTERM");
        } else {
            printf("VM %d (%s) terminée.\n", courant->id, courant->nom_executable);
        }
        free(courant);
    } else {
        printf("VM avec ID %d non trouvée.\n", id);
    }
    pthread_mutex_unlock(&mutex);
}

// Fonction pour lister les VMS dans une plage spécifiée
void lister_vms(int range_start, int range_end) {
    pthread_mutex_lock(&mutex);
    VM* courant = tete;
    printf("Liste des VMS de %d à %d:\n", range_start, range_end);
    while (courant != NULL) {
        if (courant->id >= range_start && courant->id <= range_end) {
            printf("VM ID: %d, Executable: %s, PID: %d\n", courant->id, courant->nom_executable, courant->pid);
        }
        courant = courant->suivant;
    }
    pthread_mutex_unlock(&mutex);
}

// Fonction pour lister les fichiers .olc3 dans le répertoire courant
void lister_fichiers_olc3() {
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    if (d) {
        printf("Liste des fichiers .olc3 dans le répertoire courant:\n");
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".olc3") != NULL) {
                printf("%s\n", dir->d_name);
            }
        }
        closedir(d);
    } else {
        perror("Erreur lors de l'ouverture du répertoire");
    }
}

// Fonction pour exécuter un fichier .olc3
pid_t executer_olc3(const char* executable) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("Erreur lors du fork");
        return -1;
    }
    if (pid == 0) {
        // Processus enfant
        execl(executable, executable, (char *)NULL);
        // Si execl échoue
        perror("Erreur lors de l'exécution de l'exécutable");
        exit(EXIT_FAILURE);
    }
    // Processus parent
    return pid;
}

// Fonction pour terminer un processus par PID
void terminer_processus(pid_t pid) {
    if (kill(pid, SIGTERM) == -1) {
        perror("Erreur lors de l'envoi du signal SIGTERM");
    } else {
        printf("Processus avec PID %d terminé.\n", pid);
    }
}

// Fonction de traitement d'une transaction
void* traiter_transaction(void* arg) {
    Transaction* trans = (Transaction*)arg;
// Dans la fonction traiter_transaction

switch (trans->type) {
    case 'B':
        if (user_role != USER && user_role != ADMIN) {
            printf("Transaction B refusée : permissions insuffisantes.\n");
            break;
        }
        printf("Transaction B: Listing des fichiers .olc3.\n");
        lister_fichiers_olc3();
        break;

    case 'L':
        if (user_role != ADMIN) {
            printf("Transaction L refusée : permissions insuffisantes.\n");
            break;
        }
        printf("Transaction L: Listing des VMS de %d à %d.\n", trans->param1, trans->param2);
        lister_vms(trans->param1, trans->param2);
        break;

    case 'X':
        if (user_role != USER && user_role != ADMIN) {
            printf("Transaction X refusée : permissions insuffisantes.\n");
            break;
        }
        printf("Transaction X: Exécution de %s.\n", trans->executable);
        {
            pid_t pid = executer_olc3(trans->executable);
            if (pid > 0) {
                ajouter_vm(trans->param1, trans->executable, pid);
                printf("VM %d lancée avec PID %d.\n", trans->param1, pid);
            } else {
                printf("Échec de l'exécution de %s.\n", trans->executable);
            }
        }
        break;

    case 'E':
        if (user_role != ADMIN) {
            printf("Transaction E refusée : permissions insuffisantes.\n");
            break;
        }
        printf("Transaction E: Suppression de la VM %d.\n", trans->param1);
        supprimer_vm(trans->param1);
        break;

    case 'K':
        if (user_role != ADMIN) {
            printf("Transaction K refusée : permissions insuffisantes.\n");
            break;
        }
        printf("Transaction K: Terminaison du processus avec PID %d.\n", trans->param1);
        terminer_processus(trans->param1);
        break;

    default:
        printf("Transaction inconnue: %c\n", trans->type);
        break;
}


    free(trans);
    pthread_exit(NULL);
}
// Fonction pour parser une ligne de transaction
Transaction* parser_transaction(const char* ligne) {
    Transaction* trans = (Transaction*)malloc(sizeof(Transaction));
    if (!trans) {
        perror("Erreur d'allocation mémoire pour la transaction");
        return NULL;
    }
    memset(trans, 0, sizeof(Transaction));

    char type;
    int param1, param2;
    char executable[256];

    if (sscanf(ligne, " %c", &type) != 1) {
        printf("Erreur de parsing de la ligne: %s\n", ligne);
        free(trans);
        return NULL;
    }

    trans->type = type;

    switch (type) {
        case 'B':
            // Aucun paramètre
            break;

        case 'L':
            if (sscanf(ligne, "L %d-%d", &param1, &param2) != 2) {
                printf("Erreur de parsing pour la transaction L: %s\n", ligne);
                free(trans);
                return NULL;
            }
            trans->param1 = param1;
            trans->param2 = param2;
            break;

        case 'X':
            if (sscanf(ligne, "X %d %s", &param1, executable) != 2) {
                printf("Erreur de parsing pour la transaction X: %s\n", ligne);
                free(trans);
                return NULL;
            }
            trans->param1 = param1;
            strcpy(trans->executable, executable);
            break;

        case 'E':
            if (sscanf(ligne, "E %d", &param1) != 1) {
                printf("Erreur de parsing pour la transaction E: %s\n", ligne);
                free(trans);
                return NULL;
            }
            trans->param1 = param1;
            break;

        case 'K':
            if (sscanf(ligne, "K %d", &param1) != 1) {
                printf("Erreur de parsing pour la transaction K: %s\n", ligne);
                free(trans);
                return NULL;
            }
            trans->param1 = param1; // Utilisé comme PID
            break;

        default:
            printf("Type de transaction inconnu: %c\n", type);
            free(trans);
            return NULL;
    }

    return trans;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <admin|user> <fichier_transactions>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Définir le rôle de l'utilisateur
    if (strcmp(argv[1], "admin") == 0) {
        user_role = ADMIN;
    } else if (strcmp(argv[1], "user") == 0) {
        user_role = USER;
    } else {
        fprintf(stderr, "Rôle invalide. Utiliser 'admin' ou 'user'.\n");
        exit(EXIT_FAILURE);
    }

    // Ouvrir le fichier de transactions
    FILE* fichier = fopen(argv[2], "r");
    if (fichier == NULL) {
        perror("Erreur lors de l'ouverture du fichier de transactions");
        exit(EXIT_FAILURE);
    }

    char ligne[512];
    pthread_t thread_id;

    while (fgets(ligne, sizeof(ligne), fichier)) {
        // Supprimer le caractère de nouvelle ligne
        ligne[strcspn(ligne, "\n")] = '\0';

        // Parser la transaction
        Transaction* trans = parser_transaction(ligne);
        if (trans == NULL) {
            continue; // Passer à la ligne suivante en cas d'erreur de parsing
        }

        // Créer un thread pour traiter la transaction
        if (pthread_create(&thread_id, NULL, traiter_transaction, (void*)trans) != 0) {
            perror("Erreur lors de la création du thread");
            free(trans);
        } else {
            // Détacher le thread pour qu'il libère ses ressources à la fin
            pthread_detach(thread_id);
        }
    }

    fclose(fichier);

    // Attendre que toutes les transactions soient traitées
    // Une méthode plus robuste impliquerait l'utilisation de compteurs ou de mécanismes de synchronisation
    // Pour simplifier, utiliser un délai suffisant
    sleep(5);

    // Libérer la liste des VMS
    pthread_mutex_lock(&mutex);
    VM* courant = tete;
    while (courant != NULL) {
        VM* temp = courant;
        courant = courant->suivant;
        // Terminer les processus restants
        if (kill(temp->pid, SIGTERM) == -1) {
            perror("Erreur lors de l'envoi du signal SIGTERM");
        }
        free(temp);
    }
    pthread_mutex_unlock(&mutex);

    pthread_mutex_destroy(&mutex);

    return 0;
}

