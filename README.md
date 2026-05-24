# 🚪 Portail Coulissant Intelligent basé sur ESP32

## Description

Ce projet consiste à réaliser un système intelligent de contrôle d’un portail coulissant automatisé utilisant une carte ESP32.

Le portail peut être commandé de différentes manières :

- Bouton poussoir
- Télécommande infrarouge (IR)
- Application Web

Le système permet le contrôle du moteur, la surveillance de l’état du portail, la gestion de la sécurité et l’enregistrement des événements avec date et heure.

---

## Fonctionnalités

### Contrôle du portail

Le portail peut être :

- Ouvert
- Fermé
- Arrêté à tout moment
- Contrôlé depuis plusieurs interfaces

Sources de commande :

- Bouton poussoir
- Télécommande IR
- Interface Web

---

### Gestion du moteur

Le système assure :

- Rotation dans le sens ouverture
- Rotation dans le sens fermeture
- Arrêt immédiat
- Arrêt automatique aux fins de course
- Réglage de la vitesse du moteur
- Surveillance pendant le déplacement

Pendant le mouvement :

- Activation du buzzer
- LED rouge clignotante

---

### Indication visuelle

| État du portail | Indication |
|-----------------|-------------|
| Portail fermé | 🔴 LED rouge fixe |
| Portail ouvert | 🟢 LED verte fixe |
| Portail en mouvement | 🔴 LED rouge clignotante |
| Obstacle détecté | 🔴 LED rouge clignotement rapide |

---

### Détection de position

La position du portail est déterminée grâce à :

- Fin de course ouverture
- Fin de course fermeture

États possibles :

- Ouvert
- Fermé
- En mouvement
- Position inconnue

---

### Initialisation automatique du système

Lors du démarrage du système :

Si aucun capteur de fin de course n'est actif :

```txt
Ouverture automatique

↓

Recherche fin de course ouverture

↓

Fermeture automatique

↓

Recherche fin de course fermeture

↓

Initialisation terminée
```

Cette procédure permet au système de recalibrer automatiquement la position réelle du portail après une coupure électrique ou un redémarrage.

---

### Sécurité obstacle

Le système utilise un capteur ultrason pour surveiller la présence d'obstacles pendant la fermeture du portail.

Fonctionnement :

```txt
Obstacle détecté

↓

Arrêt moteur

↓

Buzzer actif

↓

LED rouge clignotement rapide

↓

Réouverture automatique

↓

Enregistrement événement
```

---

### Gestion date et heure

Le module RTC permet :

- Date réelle
- Heure réelle
- Horodatage des événements

---

### Journal des événements

Chaque action est enregistrée automatiquement :

- Date
- Heure
- Action exécutée
- Source de commande utilisée

Exemple :

| Date | Heure | Action | Source |
|-------|--------|---------|---------|
|24/05/2026|14:30:12|Ouverture|Bouton|
|24/05/2026|14:32:05|Fermeture|Application Web|
|24/05/2026|14:35:42|Ouverture|Télécommande IR|
|24/05/2026|14:36:17|Obstacle détecté|Ultrason|

---

### Fermeture automatique

Après ouverture :

- Démarrage d'une temporisation configurable
- Attente d'une nouvelle commande
- Fermeture automatique si aucune action n'est reçue

La valeur de temporisation peut être modifiée depuis l'application Web.

---

### Interface Web

L'application Web permet :

- Ouvrir le portail
- Fermer le portail
- Stop
- Ouvrir/Fermer
- Visualiser l'état du portail
- Visualiser date et heure
- Visualiser l'historique
- Régler la fermeture automatique
- Régler la vitesse moteur
