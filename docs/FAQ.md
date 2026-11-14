# FAQ

[← Back to README](../README.md)

**Do I need a PSN account?**
No. ORBIT signs in with a key stored on your PS3.

**Do I need to change router settings or run a PC?**
No. Consoles on the same network find each other; over the internet ORBIT uses a built-in tunnel. If your router allows UPnP, ORBIT uses that automatically.

**Is there an ORBIT server that stores my messages?**
No. Every server and private chat lives on the PS3 that created it.

**Why is a server offline sometimes?**
It is only reachable while ORBIT is running on its host PS3. Messages you write meanwhile wait on your PS3 and are delivered later.

**Can I talk while playing a game?**
No. The PS3 runs one application at a time, so ORBIT stops when a game starts. Voice does keep running while the XMB is open.

**Can I use my PS5 controller?**
Its **microphone** – yes (connected by USB, handled by the PS3). Its **speaker** – no; listen through your TV or another PS3 audio output. See [AUDIO.md](AUDIO.md).

**Which languages are supported?**
Interface: English, German, French, Spanish (automatic from the PS3 system language, otherwise English). The bundled font has no Japanese, Korean or Chinese characters.

**Can ORBIT show which game my friends are playing?**
No – that is not possible under homebrew conditions. You can set a *favourite game* in your profile.

**Is it end-to-end encrypted?**
Connections between consoles are encrypted (TLS 1.2), but the host PS3 can read the messages it hosts. See [PRIVACY.md](PRIVACY.md).

**Can I go back from 1.0.3 to 0.9.5?**
No. 0.9.5 cannot read the converted data. Back up `/dev_hdd0/game/ORB000001/USRDIR/data` before updating if you might want to return.

**Can I find someone by their Orbit ID only?**
No – that would need a directory server. Use the friend code.

**What are the limits?**
Per PS3: 48 slots for servers, private chats and the friend inbox together, 32 people, 512 stored messages, up to 16 connected remote PS3s. Per server: 16 channels, 8 categories, 8 roles.

**Is ORBIT made by Sony?**
No. ORBIT is an independent homebrew project, not affiliated with Sony.

**Was AI used to make ORBIT?**
Yes, extensively – see the README. The concept, decisions and hardware testing come from the human creator.
