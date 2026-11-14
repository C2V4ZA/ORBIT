<div align="center">

<img src="assets/branding/orbit-banner.svg" alt="ORBIT — Comunicación moderna para PlayStation 3" width="100%">

<br>

**Texto. Voz. Amigos. Comunidades. Una PS3 conectada de verdad.**

[![Última versión](https://img.shields.io/github/v/release/C2V4ZA/ORBIT?label=%C3%BAltima&color=4f8cff&style=flat-square)](https://github.com/C2V4ZA/ORBIT/releases/latest)
![Plataforma](https://img.shields.io/badge/plataforma-PlayStation%203-0d0f13?style=flat-square)
![CFW / HEN](https://img.shields.io/badge/funciona%20en-CFW%20%7C%20HEN-a66bff?style=flat-square)
![Probado en PS3](https://img.shields.io/badge/1.0.3-probado%20en%20una%20PS3%20real-39d98a?style=flat-square)
[![Licencia](https://img.shields.io/badge/licencia-GPL--3.0--or--later-4f8cff?style=flat-square)](LICENSE)

[English](README.md) · [Deutsch](README-DE.md) · [Français](README-FR.md) · **Español**

[**⬇ Descargar ORBIT 1.0.3**](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3) &nbsp;·&nbsp; [Instalación](docs/INSTALLATION.md) &nbsp;·&nbsp; [Funciones](docs/FEATURES.md) &nbsp;·&nbsp; [FAQ](docs/FAQ.md)

</div>

---

> [!IMPORTANT]
> **Nueva cuenta oficial.** ORBIT se publicó primero por su creador en la cuenta de GitHub **C2V3ZA**. El acceso a esa cuenta se perdió por un problema con su correo electrónico y la cuenta fue eliminada. **C2V4ZA es la nueva cuenta oficial del mismo creador**: no es un fork ni una copia, y ORBIT no está abandonado. [Leer el anuncio completo](docs/ACCOUNT-MIGRATION.md#español).

## ¿Qué es ORBIT?

ORBIT es una aplicación homebrew independiente que lleva la comunicación moderna a la PlayStation 3: chat de texto, chat de voz, amigos, mensajes privados y servidores de comunidad con canales y roles, directamente de PS3 a PS3.

- **Sin cuenta ni contraseña.** Cada PS3 inicia sesión con su propia clave secreta.
- **Sin tocar el router ni usar un PC.** En la misma red las consolas se encuentran solas; por internet ORBIT abre su propio túnel.
- **Tus datos se quedan en las PS3.** Cada servidor vive en la PS3 que lo creó. No existe un servidor de empresa de ORBIT.
- **Habla tu idioma.** La interfaz sigue el idioma del sistema de la PS3: español, inglés, alemán o francés.

<div align="center">
<img src="docs/screenshots/languages/servers-es.png" alt="ORBIT 1.0.3 en español: servidor de comunidad con categorías, canales de texto y voz y chat" width="88%">
<br><sub>Servidor de comunidad en ORBIT 1.0.3, generado por el código de interfaz real de la aplicación con datos de ejemplo.</sub>
</div>

## Novedades de la 1.0.3

| | |
|---|---|
| **Amigos** | Código de amigo de 20 caracteres, solicitudes, aceptar/rechazar, eliminar, bloquear, un ID de Orbit permanente por PS3 |
| **Mensajes** | Chats privados y de grupo, respuestas, edición, borrado, 6 reacciones, búsqueda, silenciar, marcas de tiempo |
| **Mensajes sin conexión** | Los mensajes esperan en tu PS3 («Esperando entrega») y se entregan cuando la otra PS3 está disponible |
| **Servidores de comunidad** | Categorías, canales de texto y voz, plantillas, descripción, mensaje de bienvenida, icono, color, banner, público o privado |
| **Roles y permisos** | Owner, Admin, Moderator, Member + roles propios, 10 permisos, comprobados por la PS3 anfitriona |
| **Canales de voz** | Cambiar de canal, silenciar a alguien solo para ti, volumen por persona de 0 a 200 %, silencio de servidor por moderadores |
| **Perfiles** | Avatar, color, sobre mí, juego favorito, estado personalizado, En línea / Ausente / Ocupado / Invisible |
| **Notificaciones** | Solicitudes, mensajes privados, menciones, llamadas, invitaciones, moderación; cada tipo se puede desactivar |
| **Temas** | Dark, Blue, Purple, Minimal |
| **Idiomas** | Español, inglés, alemán, francés, automáticamente según el idioma de la PS3 |

Lista completa: [docs/FEATURES.md](docs/FEATURES.md) (inglés)

## Capturas de pantalla

<table>
<tr>
<td width="50%"><img src="docs/screenshots/home.png" alt="Inicio: amigos en línea, solicitudes, chats sin leer y actividad reciente"><br><sub><b>Inicio</b>: amigos, chats y servidores de un vistazo</sub></td>
<td width="50%"><img src="docs/screenshots/friends.png" alt="Perfil de un amigo con estado, ID de Orbit, sobre mí, juego favorito y servidores en común"><br><sub><b>Amigos</b>: perfil, estado, ID de Orbit, servidores en común</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/messages.png" alt="Conversación privada entre amigos"><br><sub><b>Mensajes</b>: chats privados y de grupo</sub></td>
<td><img src="docs/screenshots/server-overview.png" alt="Resumen de un servidor con banner, descripción y miembros"><br><sub><b>Servidores de comunidad</b>: resumen, miembros, roles</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/voice.png" alt="Canal de voz con los participantes"><br><sub><b>Chat de voz</b>: canales de voz con participantes</sub></td>
<td><img src="docs/screenshots/settings.png" alt="Ajustes: el idioma sigue el idioma del sistema de la PS3"><br><sub><b>Ajustes</b>: idioma, apariencia, notificaciones, audio, privacidad</sub></td>
</tr>
</table>

<sub>Todas las capturas proceden del motor de interfaz de ORBIT 1.0.3 (el mismo código C que en la PS3) con nombres y mensajes de ejemplo.</sub>

## Descargar e instalar

1. Descarga **[Orbit-PS3-1.0.3.pkg](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3)** (ID de título `ORB000001`).
2. Cópialo a un USB en FAT32 e instálalo con el **Package Manager** en una PS3 con **CFW o HEN**.
3. Abre **Orbit Chat** desde la columna Juego y confirma tu nombre.

Guía detallada: **[docs/INSTALLATION.md](docs/INSTALLATION.md)** (inglés)

> [!WARNING]
> Al actualizar desde la 0.9.5 se conservan grupos, mensajes y nombre; los grupos se convierten en servidores. **No es posible volver de la 1.0.3 a la 0.9.5.**

## Mando PS5

Puedes utilizar el micrófono del mando PS5 para hablar en ORBIT. Sin embargo, las voces de otros participantes no se pueden reproducir a través del altavoz integrado del mando. Utiliza el televisor u otra salida de audio compatible. Más: [docs/AUDIO.md](docs/AUDIO.md)

## Conviene saber

- Un servidor solo está disponible mientras ORBIT se ejecuta en la PS3 que lo aloja.
- Las conexiones entre consolas están cifradas, pero **la PS3 anfitriona puede leer los mensajes de sus propios servidores y chats privados**: ORBIT no promete cifrado de extremo a extremo estricto. Ver [docs/PRIVACY.md](docs/PRIVACY.md).
- No hay chat de voz durante un juego (la PS3 ejecuta una sola aplicación a la vez).
- Límites y problemas conocidos: [docs/FAQ.md](docs/FAQ.md) · [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

## Desarrollado con ayuda de IA

ORBIT se ha desarrollado con **una ayuda amplia de inteligencia artificial**: arquitectura, programación en C, funciones, interfaz, red, audio, búsqueda de errores, pruebas, traducciones, documentación y preparación de versiones. La idea, la dirección creativa y las decisiones importantes son del creador humano, que además prueba cada versión en una PlayStation 3 real. Lo decimos abiertamente porque tienes derecho a saber cómo se hizo el software que instalas.

## Licencia y aviso legal

ORBIT es software libre bajo la licencia [GPL-3.0-or-later](LICENSE). Componentes de terceros y sus licencias: [THIRD-PARTY.md](THIRD-PARTY.md).

ORBIT es un proyecto homebrew independiente y **no tiene relación con Sony Interactive Entertainment**. «PlayStation» y «PS3» son marcas de sus respectivos propietarios y se mencionan solo para describir la compatibilidad.

<div align="center"><br><img src="assets/branding/orbit-mark.svg" alt="Logo de ORBIT" width="56"><br><sub>ORBIT — Volviendo a conectar a los jugadores de PlayStation 3.</sub></div>
