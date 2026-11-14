"""Collect reproducible source and user-facing artifacts without private runtime data."""
from pathlib import Path
import hashlib,json,shutil,zipfile
root=Path(__file__).resolve().parents[1]
out=root.parents[1]/'outputs'/'Orbit-PS3-0.6'
out.mkdir(parents=True,exist_ok=True)
for source,name in [('build/Orbit-PS3-0.6-preview.pkg','Orbit-PS3-0.6-preview.pkg'),('README-DE.md','Anleitung-DE.md'),('build/ui-chat.png','Oberflaeche-Beispieldaten.png'),('build/ui-home.png','Startbildschirm.png'),('build/ui-overview.png','Ansichten-Uebersicht.png'),('build/Orbit-Audio-Test-0.2.pkg','Orbit-Audio-Test-0.2.pkg'),('assets/orbit-chat-logo.svg','Orbit-Chat-Logo.svg')]:
    shutil.copy2(root/source,out/name)
with zipfile.ZipFile(out/'Alle-Ansichten-Beispieldaten.zip','w',zipfile.ZIP_DEFLATED) as z:
    for file in sorted((root/'build').glob('ui-*.png')):
        z.write(file,file.name)
with zipfile.ZipFile(out/'Orbit-PS3-0.6-Quellcode.zip','w',zipfile.ZIP_DEFLATED) as z:
    for folder in ['src','include','assets','vendor','tools','tests']:
        for file in sorted((root/folder).rglob('*')):
            if file.is_file() and '__pycache__' not in file.parts:
                z.write(file,'orbit-ps3/'+str(file.relative_to(root)).replace('\\','/'))
    for name in ['Makefile','README-DE.md','THIRD-PARTY.md','LICENSE','.clang-format']:
        z.write(root/name,'orbit-ps3/'+name)
report={
 'version':'0.6-preview', 'date':'2026-11-08', 'title_id':'ORB000001',
 'toolchain':'ps3dev nightly-2026-10-12, PPU GCC 13.2.0',
 'previous_05_change_scope':'Bounded glyph cache, unchanged-state redraw suppression, supported 720p selection, controller repeat, R3 performance display, 8-character group codes with legacy compatibility/rate limits, direct cellMic input probe.',
 'change_scope':'Persistent login with per-device tokens, automatic reconnect, combined server/group invitations, queued email invitations before registration, shared host identity, clearer join and sign-out navigation.',
 'ui_review':'Actual native renderer with fixture data: chat/menu/inbox/login overview, public endpoint page and audio probe reviewed at 1280x720; long history and dense states included. Not hardware tests.',
 'renderer_comparison':(root/'build/render-cache-test.log').read_text().strip(),
 'desktop_benchmark_before':(root/'build/benchmark-before.txt').read_text().splitlines(),
 'desktop_benchmark_after':(root/'build/benchmark-after.txt').read_text().splitlines(),
 'benchmark_limit':'Development-machine render times only; not PS3 FPS.',
 'hardware_feedback':'User reports working microphone input meter in audio probe 0.2; no confirmation of playback, voice calls or background operation.',
 'router_observation':'Gateway landing page reachable at 192.168.0.1; read-only public status request returned HTTP 500. WAN mode unknown. No router settings changed.',
 'tests': [line for line in (root/'build/verify-06.log').read_text().splitlines() if line.startswith('PASS:')],
 'package_checks':[line for line in (root/'build/package.log').read_text().splitlines() if line.startswith('PASS:')],
 'audio_probe_package_checks':[line for line in (root/'build/probe-package.log').read_text().splitlines() if 'PASS:' in line],
 'audio_probe':'Direct cellMic raw input test, byte counter and 16-bit peak meter. Replaces IN_MIC voice-port route after user reported 0x80310809. No playback or networking.',
 'internet_test':'IPv4 and DNS connections tested locally; no test over two public networks; no NAT traversal or router configuration.',
 'hardware_tested':False, 'cfw_hen_verified':False, 'measured_ps3_fps':None,
 'voice_implemented':False,'in_game_background_implemented':False,'dualsense_audio_implemented':False,
 'qr_mode':'combined server and optional group invitation text, not browser login',
 'runtime_server':'inside foreground PS3 application',
 'files':{p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.iterdir()) if p.is_file() and p.suffix in ['.pkg','.zip','.png','.svg']}
}
(out/'Build-und-Testnachweis.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
(out/'SHA256SUMS.txt').write_text(''.join(v['sha256']+'  '+k+'\n' for k,v in report['files'].items()),encoding='ascii')
print(out)
for p in sorted(out.iterdir()):print(p.name,p.stat().st_size)
