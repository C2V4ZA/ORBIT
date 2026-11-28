<div align="center">

<img src="assets/branding/orbit-banner.svg" alt="ORBIT — Comunicación moderna para PlayStation 3 y PlayStation 4" width="100%">

<br>

**Texto. Voz. Amigos. Comunidades. PS3 y PS4, conectadas de verdad.**

[![Última versión](https://img.shields.io/github/v/release/C2V4ZA/ORBIT?label=%C3%BAltima&color=4f8cff&style=flat-square)](https://github.com/C2V4ZA/ORBIT/releases/latest)
![Plataforma](https://img.shields.io/badge/plataforma-PlayStation%203-0d0f13?style=flat-square)
![CFW / HEN](https://img.shields.io/badge/funciona%20en-CFW%20%7C%20HEN-a66bff?style=flat-square)
![Idiomas](https://img.shields.io/badge/idiomas-9-39d98a?style=flat-square)
[![Licencia](https://img.shields.io/badge/licencia-GPL--3.0--or--later-4f8cff?style=flat-square)](LICENSE)

[English](README.md) · [Deutsch](README-DE.md) · [Français](README-FR.md) · **Español**

[**⬇ Descargar ORBIT 1.3.0**](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.3.0) &nbsp;·&nbsp; [Instalación](docs/INSTALLATION.md) &nbsp;·&nbsp; [Funciones](docs/FEATURES.md) &nbsp;·&nbsp; [FAQ](docs/FAQ.md) &nbsp;·&nbsp; [Cambios](CHANGELOG.md)

</div>

---

> [!IMPORTANT]
> **Nueva cuenta oficial.** ORBIT se publicó primero por su creador en la cuenta de GitHub **C2V3ZA**. El acceso a esa cuenta se perdió por un problema con su correo electrónico y la cuenta fue eliminada. **C2V4ZA es la nueva cuenta oficial del mismo creador**: no es un fork ni una copia, y ORBIT no está abandonado. [Leer el anuncio completo](docs/ACCOUNT-MIGRATION.md#español).

## ¿Qué es ORBIT?

ORBIT es una aplicación homebrew independiente que lleva la comunicación moderna a la PlayStation 3 y la PlayStation 4: chat de texto, chat de voz, amigos, mensajes privados y servidores de comunidad con canales y roles, de consola a consola, con crossplay PS3 ↔ PS4.

- **Sin cuenta, sin contraseña.** Cada consola se identifica con su propia clave secreta.
- **Sin ajustes del router, sin PC.** Por Internet, ORBIT abre su propio túnel. El **Privacy Shield** (activado por defecto) garantiza que ninguna otra consola se conecte directamente a tu dirección IP.
- **Tus datos se quedan en tus consolas.** Cada servidor vive en la PS3 o PS4 que lo creó. No existe ningún servidor de empresa de ORBIT.
- **Habla tu idioma.** 10 idiomas, elegidos automáticamente según el idioma del sistema.

<div align="center">
<img src="docs/screenshots/languages/home-es.png" alt="ORBIT 1.3.0 en español: inicio con Spotlight, amigos en línea, mensajes y tus servidores" width="88%">
<br><sub>Inicio de ORBIT 1.3.0, generado por el propio código de interfaz de la aplicación con datos de ejemplo.</sub>
</div>

## Novedades de la 1.3.0

| | |
|---|---|
| **ORBIT para PS4** | Primera versión pública para PS4 con jailbreak (GoldHEN), compilada con la cadena libre OpenOrbis – 1080p nativo, transiciones, cristal esmerilado. Experimental. Código: [`ps4/`](ps4/) |
| **Crossplay PS3 ↔ PS4** | Mismos servidores, amigos, chats privados y canales de voz; pequeña etiqueta PS3 / PS4 junto a los nombres |
| **Descubrir entre consolas** | Servidores públicos de PS3 y PS4 en una lista, cada uno con su etiqueta |
| **Privacy Shield** | Activado por defecto: ninguna consola se conecta directamente a tu IP – todo pasa por el túnel Orbit o un relé. [Detalles](docs/PRIVACY-SHIELD.md) |
| **Secreto perfecto hacia adelante** | Una clave nueva por conexión entre dos consolas 1.3 |
| **Portadas: primero preguntar** | Las portadas que ya están en la consola se muestran siempre; las oficiales solo se descargan tras tu *Sí* |
| **Mensajes en segundo plano en PS4** | Plugin GoldHEN experimental: mensajes nuevos como notificación mientras juegas |
| **Correcciones** | La voz podía convertirse en ruido entre compiladores distintos (PS3 ↔ PS4), fallo del Shield en el plugin de notificaciones de PS3, seguridad de hilos, salida de audio de PS4 |

> [!NOTE]
> **La 1.3.0 es una versión preliminar, aún no probada en una PS3 o PS4 real** (solo en PC: 107 pruebas automáticas, sanitizers, fuzzing). Incluye todo lo de la 1.2.1, que no se publicó por separado. Informa de problemas en [Issues](https://github.com/C2V4ZA/ORBIT/issues).

## Novedades de la 1.2.1

| | |
|---|---|
| **Portadas de juegos** | Las portadas de tus juegos instalados, descargadas en tu PS3 desde [GameTDB](https://www.gametdb.com), primero la de tu idioma |
| **Página Juegos** | Mosaicos verticales llenos con la portada; ✕ sobre un juego abre *Buscar servidores de este juego*, *Invitar amigos*, *Descubrir comunidades* |
| **Inicio** | Muestra **automáticamente tu último juego jugado**; nueva fila **Tus servidores** para entrar rápido a tus comunidades |
| **Salir de servidores** | Funciona aunque la PS3 anfitriona esté apagada; se le avisa cuando vuelva |
| **Polski** | 10.º idioma, con teclado polaco y sugerencias de palabras |
| **Correcciones** | Los textos largos en ruso/ucraniano ya no se cortan, textos UTF-8 seguros en todas partes, menos redibujados |

> [!NOTE]
> **La 1.2.1 es una versión preliminar.** Probada en PC (pruebas automáticas, sanitizers) y en el emulador RPCS3, aún no en una PS3 real. Informa de problemas en [Issues](https://github.com/C2V4ZA/ORBIT/issues). ORBIT 1.2.0 ofrece esta actualización por sí mismo: *Ajustes › Acerca de Orbit › Buscar actualizaciones*.

## Novedades de la 1.2.0

| | |
|---|---|
| **Nueva interfaz** | Diseño de cristal, barra superior común (L1/R1), inicio Spotlight, 8 temas, nueva intro, sonidos, teclado de emojis, No molestar |
| **Descubrir** | Comunidades públicas en PS3 conectadas: búsqueda, filtros, orden, detalles, indicadores en línea/desconectado, lista guardada cuando un anfitrión está desconectado |
| **Juegos** | Tus juegos de PS3 instalados, y qué amigos los indican o los están jugando |
| **Notificaciones en segundo plano** | Los mensajes nuevos aparecen arriba a la derecha en el XMB y en los juegos, incluso con ORBIT cerrado (plugin) |
| **Fotos de perfil** | Desde un USB o desde tu **móvil** (escanea el código QR) |
| **Imágenes animadas** | Avatares GIF e iconos de servidor (máx. 10 fps, 15 s, 150 fotogramas), pausa con carga alta, ajuste *Reducir movimiento* |
| **Apariencia del servidor** | Icono fijo o animado, símbolo, color, banner, restablecer, vista previa con recorte, **registro de cambios**: solo el propietario y los roles con *Gestionar servidor*, comprobado por el anfitrión |
| **Moderación** | Denuncias, lista de denuncias para moderadores, expulsiones temporales (1 / 7 / 30 días / hasta retirarlas), filtro de nombres |
| **Idiomas** | Español, English, Deutsch, Français, Italiano, Português, **Русский, Українська, Norsk bokmål**: formas de plural, teclados cirílico y nórdico, sugerencias de palabras locales |
| **Actualizaciones firmadas** | ORBIT busca actualizaciones firmadas con Ed25519; nada se instala solo |

Todo lo de la 1.0.3 sigue ahí: amigos, chats privados y de grupo, mensajes sin conexión, servidores de comunidad con categorías, roles y permisos, canales de voz, perfiles. Lista completa: [CHANGELOG.md](CHANGELOG.md)

> [!NOTE]
> **La 1.2.0 es nueva.** Las notificaciones en segundo plano se confirmaron en una PS3 real. La mayoría de las demás funciones de la 1.2 se probaron en el PC (pruebas automáticas, sanitizers, fuzzing) y aún se están probando en una PS3 real. Informa de los problemas en [Issues](https://github.com/C2V4ZA/ORBIT/issues).

## Capturas de pantalla

<table>
<tr>
<td width="50%"><img src="docs/screenshots/discover.png" alt="Descubrir: tarjetas de comunidades con indicadores en línea y anfitrión desconectado"><br><sub><b>Descubrir</b>: encuentra comunidades de PS3 y PS4, incluso con el anfitrión desconectado</sub></td>
<td width="50%"><img src="docs/screenshots/games.png" alt="Juegos: cuadrícula de juegos de PS3 instalados"><br><sub><b>Juegos</b>: tus juegos de PS3 y a qué juegan tus amigos</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/servers.png" alt="Servidor de comunidad con categorías, canales de texto y voz y un chat"><br><sub><b>Servidores de comunidad</b>: canales, roles, voz</sub></td>
<td><img src="docs/screenshots/phone-picture.png" alt="Foto de perfil desde el móvil: código QR y espera"><br><sub><b>Foto desde tu móvil</b>: escanea, elige, listo</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/privacy-shield.png" alt="Ajustes de privacidad con el Privacy Shield activado"><br><sub><b>Privacy Shield</b> – tu IP queda oculta, solo por túnel o relé</sub></td>
<td><img src="docs/screenshots/covers-question.png" alt="Pregunta al primer inicio sobre las portadas oficiales"><br><sub><b>Portadas</b> – pregunta antes de descargar</sub></td>
</tr>
</table>

<sub>Todas las capturas proceden del motor de interfaz de ORBIT 1.3.0 (el mismo código C que se ejecuta en la PS3) con nombres y mensajes de ejemplo.</sub>

## Idiomas

Completos: **Español, English, Deutsch, Français, Italiano, Português, Русский, Українська, Norsk bokmål, Polski** (nuevo en la 1.2.1).

La página de idioma también muestra los idiomas que **aún no están disponibles**, y por qué: el japonés, el coreano, el chino y el hindi necesitan una fuente que ORBIT todavía no incluye; el árabe necesita texto de derecha a izquierda y unión de letras; los demás idiomas europeos aún no están traducidos. ORBIT nunca marca un idioma como completo si no lo está.

## Próximamente: servidores 24/7

Hoy un servidor solo está en línea mientras ORBIT se ejecuta en la PS3 anfitriona. Estamos trabajando en un **servidor ORBIT siempre activo**: las comunidades **seguirán en línea aunque la PS3 del anfitrión esté apagada**, con mensajes, miembros y canales accesibles a cualquier hora. Será opcional; ORBIT seguirá funcionando de PS3 a PS3 como hasta ahora.

## Descargar e instalar

1. Descarga **[Orbit-PS3-1.3.0.pkg](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.3.0)** (ID de título `ORB000001`).
2. Cópialo a un USB en FAT32 e instálalo con el **Package Manager** en una PS3 con **CFW o HEN**.
3. Abre **Orbit Chat** desde la columna Juego.

Tus datos de la 1.0.x se conservan. Guía detallada (inglés): **[docs/INSTALLATION.md](docs/INSTALLATION.md)**

> [!WARNING]
> Al actualizar se conservan tus amigos, servidores, mensajes y nombre. **No es posible volver a una versión anterior.**

## Notificaciones en segundo plano, presencia en juegos y portadas

No hay que descargar nada más: **los plugins vienen dentro del paquete de ORBIT** y ORBIT los instala por ti.

**Notificaciones en segundo plano** (mensajes nuevos arriba a la derecha en el XMB y en los juegos, aunque ORBIT esté cerrado)
1. Abre ORBIT › **Ajustes › Notificaciones** › **Instalar el plugin en segundo plano**.
2. Activa **Mostrar mensajes nuevos arriba a la derecha** (y si quieres *Mostrar el texto del mensaje*).
3. **Reinicia la PS3.** Necesita Cobra/webMAN (CFW) o PS3HEN: ORBIT crea `/dev_hdd0/plugins/orbit_notify.sprx` y lo añade a `boot_plugins.txt` / `boot_plugins_hen.txt`.
4. Para quitarlo: mismo menú › **Quitar el plugin en segundo plano** y reinicia.

**Presencia en juegos** (experimental, solo amigos, desactivada por defecto): Ajustes › Notificaciones › **Instalar el plugin de detección** (un `orbit_presence.sprx` aparte) y reinicia la PS3. Aún no probado en una PS3 real.

**Portadas**: nada que instalar. Con Internet, ORBIT descarga la portada de cada juego instalado por su Title ID desde [GameTDB](https://www.gametdb.com); solo el Title ID sale de tu PS3. Desactivar o recargar todas: **Ajustes › Privacidad** (*Descargar portadas de juegos*, *Actualizar portadas*). Los juegos sin portada en GameTDB mantienen su icono normal; las portadas que faltan se pueden añadir en GameTDB. El paquete no incluye imágenes de portadas.

## Conviene saber

- Un servidor solo es accesible mientras ORBIT se ejecuta en la PS3 que lo aloja (los servidores 24/7 están previstos, ver arriba).
- Las conexiones entre consolas están cifradas, pero **la PS3 anfitriona puede leer los mensajes de sus propios servidores y chats privados**: ORBIT no promete cifrado de extremo a extremo. Ver [docs/PRIVACY.md](docs/PRIVACY.md).
- Las imágenes se comprueban con los lectores estrictos de PNG, JPEG y GIF de ORBIT; solo una versión recodificada de 96 × 96 sale de tu PS3, nunca el archivo original.
- No hay chat de voz durante un juego (la PS3 ejecuta una sola aplicación a la vez). Las notificaciones en segundo plano sí funcionan en los juegos.
- El micrófono del mando de PS5 se puede usar para hablar; su altavoz no puede reproducir el chat de voz. Detalles: [docs/AUDIO.md](docs/AUDIO.md)
- Todos los límites y problemas conocidos: [docs/FAQ.md](docs/FAQ.md) · [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

## Creado con ayuda de IA

ORBIT se ha desarrollado con **una amplia ayuda de inteligencia artificial**: arquitectura, programación en C, funciones, interfaz, red, audio, búsqueda de errores, pruebas, traducciones, documentación y preparación de versiones. La idea, la dirección creativa y las decisiones importantes vienen del creador humano, que prueba las versiones en hardware real de PlayStation 3. Lo decimos abiertamente porque debes saber cómo se ha creado el software que instalas.

## Licencia y aviso legal

ORBIT es software libre bajo la [GPL-3.0-or-later](LICENSE). Componentes de terceros y sus licencias: [THIRD-PARTY.md](THIRD-PARTY.md).

ORBIT es un proyecto homebrew independiente y **no está afiliado, respaldado ni vinculado a Sony Interactive Entertainment**. «PlayStation» y «PS3» son marcas de sus respectivos propietarios y se usan aquí solo para describir la compatibilidad.

<div align="center"><br><img src="assets/branding/orbit-mark.svg" alt="Logotipo de ORBIT" width="56"><br><sub>ORBIT — Volver a conectar a los jugadores de PlayStation 3.</sub></div>
