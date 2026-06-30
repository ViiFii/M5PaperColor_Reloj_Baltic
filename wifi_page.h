// Web page for WI-FI MODE (microSD manager + graphical config).
// Kept in a separate .h because the Arduino .ino preprocessor breaks large raw strings.
#pragma once

static const char WIFI_HTML[] PROGMEM = R"HTMLPAGE(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PaperColor SD</title><style>
:root{--bg:#0e1116;--card:#171b22;--card2:#1e242e;--line:#2a313c;--txt:#e7ebf0;--mut:#94a3b8;--acc:#3b82f6;--acch:#2f6fe0;--red:#ef4444;--grn:#22c55e}
body.light{--bg:#f4f6fa;--card:#ffffff;--card2:#eef1f6;--line:#d7dce4;--txt:#1a2230;--mut:#5a6677;--acc:#2563eb;--acch:#1d4ed8;--red:#dc2626;--grn:#16a34a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
header{position:sticky;top:0;z-index:5;background:var(--card);padding:14px 16px;box-shadow:0 2px 10px rgba(0,0,0,.45)}
header h1{margin:0;font-size:17px;font-weight:600}
.bc{margin-top:6px;font-size:13px;color:var(--mut);word-break:break-all}
.bc a{color:var(--acc);text-decoration:none}
.tools{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:12px 14px;border-bottom:1px solid var(--line)}
.btn{display:inline-flex;align-items:center;gap:6px;background:var(--card2);color:var(--txt);border:1px solid var(--line);border-radius:10px;padding:9px 13px;font-size:14px;cursor:pointer;text-decoration:none;white-space:nowrap}
.btn:hover{background:#2a3240}
.btn.p{background:var(--acc);border-color:var(--acc);color:#fff}.btn.p:hover{background:var(--acch)}
.btn.d{color:var(--red)}
.list{padding:6px 8px 96px}
.row{display:flex;align-items:center;gap:11px;padding:11px 8px;border-radius:12px}
.row:hover{background:var(--card)}
.row .ic{font-size:22px;width:26px;text-align:center;flex-shrink:0}
.row .nm{flex:1;min-width:0;cursor:pointer}
.row .nm b{font-weight:500;display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row .nm .sz{color:var(--mut);font-size:12px;margin-top:2px}
.row .mb{flex-shrink:0;background:none;border:0;color:var(--mut);font-size:22px;line-height:1;padding:6px 10px;cursor:pointer;border-radius:8px}
.row .mb:hover{background:var(--card2);color:var(--txt)}
.up{color:var(--acc);font-weight:500}
.muted{color:var(--mut);text-align:center;padding:36px 16px}
#prog{position:fixed;top:0;left:0;right:0;height:3px;z-index:50;overflow:hidden;display:none}
#prog.on{display:block}
#prog::before{content:"";position:absolute;height:100%;width:40%;background:var(--acc);animation:pgo 1.1s linear infinite}
@keyframes pgo{0%{left:-42%}100%{left:100%}}
/* Gallery/thumbnails in masonry: each image keeps its real aspect ratio (no cropping) */
.grid{column-width:118px;column-gap:8px;padding:10px}
.grid>*{break-inside:avoid;margin:0 0 8px}
.grid img{width:100%;height:auto;display:block;border-radius:10px;cursor:pointer;background:var(--card)}
.ti{position:relative}
.vthumb{min-height:90px;background:var(--card);display:flex;align-items:center;justify-content:center}
.vbadge{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);font-size:30px;color:#fff;text-shadow:0 0 6px #000;pointer-events:none}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 8px;text-align:center;cursor:pointer;position:relative}
.tile .ti2{font-size:34px}.tile .nm2{font-size:12px;margin-top:6px;word-break:break-word;color:var(--txt)}
.tmb{position:absolute;top:6px;right:6px;background:rgba(0,0,0,.5);color:#fff;border:0;border-radius:8px;padding:1px 9px;font-size:18px;line-height:1.3;cursor:pointer}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:10px;overflow:hidden}
.seg button{background:var(--card2);color:var(--mut);border:0;padding:8px 11px;font-size:16px;line-height:1;cursor:pointer}
.seg button.on{background:var(--acc);color:#fff}
.modal{display:none;position:fixed;inset:0;z-index:20;background:rgba(8,10,14,.97);flex-direction:column}
.modal .bar{display:flex;align-items:center;gap:8px;padding:10px 12px;background:var(--card);border-bottom:1px solid var(--line)}
.modal .bar .t{font-weight:600;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.modal .bd{flex:1;overflow:auto}
.modal .bdc{flex:1;display:flex;justify-content:center;align-items:center;overflow:auto;padding:10px}
.modal .bdc img,.modal .bdc video{max-width:100%;max-height:100%;border-radius:8px}
#eparea{flex:1;background:#fff;overflow:hidden}
#eparea .muted{color:#555}
#player{display:none;position:fixed;left:0;right:0;bottom:0;z-index:15;background:var(--card);border-top:1px solid var(--line);padding:8px 10px;flex-direction:column;box-shadow:0 -2px 10px rgba(0,0,0,.4)}
#player.on{display:flex}
#player .prow{display:flex;align-items:center;gap:8px}
#player .pn{flex:1;min-width:0;font-size:13px;color:var(--mut);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#player audio{width:100%;margin-top:6px;height:38px}
.nav{position:absolute;top:50%;transform:translateY(-50%);display:none;background:rgba(0,0,0,.45);color:#fff;border:0;font-size:34px;line-height:1;width:46px;height:66px;border-radius:10px;cursor:pointer}
.nav:active{background:rgba(0,0,0,.7)}.np{left:8px}.nn{right:8px}
#ta{width:100%;height:100%;background:var(--card2);color:var(--txt);border:0;padding:14px;font:13px/1.55 ui-monospace,Consolas,monospace;resize:none;outline:none}
.st{font-size:12px;margin-right:2px;max-width:40vw;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sec{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 12px 14px;margin:10px 12px}
.sec h3{margin:0 0 10px;font-size:14px;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
.fld{margin:9px 0}.fld label{display:block;font-size:12px;color:var(--mut);margin-bottom:4px}
.fld input{width:100%;background:var(--card2);border:1px solid var(--line);color:var(--txt);border-radius:9px;padding:10px;font-size:14px}
.chk{display:flex;align-items:center;gap:9px;margin:8px 0;font-size:14px}.chk input{width:18px;height:18px}
.subrow{display:flex;gap:6px;margin-bottom:7px}.subrow input{flex:1;min-width:0;background:var(--card2);border:1px solid var(--line);color:var(--txt);border-radius:9px;padding:9px;font-size:14px}
.sheet{display:none;position:fixed;inset:0;z-index:30;background:rgba(0,0,0,.55)}
.sheet .panel{position:absolute;left:0;right:0;bottom:0;background:var(--card);border-radius:16px 16px 0 0;padding:8px 8px 16px}
.sheet .panel .h{padding:10px 12px;color:var(--mut);font-size:13px;border-bottom:1px solid var(--line);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sheet .it{display:block;width:100%;text-align:left;background:none;border:0;color:var(--txt);font-size:16px;padding:15px 14px;border-radius:10px;cursor:pointer}
.sheet .it:hover{background:var(--card2)}.sheet .it.d{color:var(--red)}
</style></head><body>

<div id="prog"></div>
<header><h1>📂 PaperColor &middot; microSD</h1><div class="bc" id="bc">/</div></header>

<div class="tools">
 <label class="btn">📤 Upload<input type="file" id="f" hidden onchange="up()"></label>
 <button class="btn" onclick="mk()">📁 New folder</button>
 <button class="btn" onclick="zipCwd()">⬇ Folder (ZIP)</button>
 <button class="btn p" onclick="cfgOpen()">⚙️ Settings</button>
 <span class="seg" id="seg" style="margin-left:auto">
  <button id="vL" onclick="setVM('list')" title="List">☰</button>
  <button id="vD" onclick="setVM('details')" title="Details">≣</button>
  <button id="vT" onclick="setVM('thumbs')" title="Thumbnails">▦</button>
 </span>
 <button class="btn" id="thb" onclick="theme()" title="Light/dark theme">🌙</button>
</div>

<div class="list" id="list"></div>

<div class="modal" id="vwr"><div class="bar"><span class="t" id="vtt">View</span><button class="btn" onclick="cv()">✕ Close</button></div>
 <div class="bdc" id="vc"></div>
 <button class="nav np" id="vprev" onclick="navImg(-1)">‹</button><button class="nav nn" id="vnext" onclick="navImg(1)">›</button></div>

<div class="modal" id="ed"><div class="bar"><span class="t" id="ettl">Edit</span><span class="st" id="est"></span>
 <button class="btn" onclick="theme()" title="Light/dark theme">🌓</button>
 <button class="btn p" onclick="sv()">💾 Save</button><button class="btn" onclick="ce()">✕</button></div><textarea id="ta" spellcheck="false"></textarea></div>

<div class="modal" id="cfg"><div class="bar"><span class="t">Settings</span><span class="st" id="cfst"></span>
 <button class="btn" onclick="cfgRaw()">{ } JSON</button><button class="btn p" onclick="cfgSave()">💾 Save</button><button class="btn" onclick="cfgClose()">✕</button></div>
 <div class="bd" id="cfgbd"></div></div>

<div class="modal" id="epov"><div class="bar"><span class="t" id="eptt">Read</span>
 <button class="btn" onclick="epPrev()">‹ Previous</button><button class="btn" onclick="epNext()">Next ›</button>
 <button class="btn" onclick="epClose()">✕ Close</button></div>
 <div id="eparea"></div></div>

<div class="sheet" id="sheet" onclick="if(event.target==this)closeSheet()"><div class="panel" id="spanel"></div></div>

<div id="player"><div class="prow"><span class="pn" id="ptt"></span><button class="btn" onclick="pstop()" style="padding:5px 11px">✕</button></div><audio id="pau" controls></audio></div>

<script>
var cwd="/";
var g_imgs=[],g_ii=0,g_isImg=false;
var g_ents=[],g_dir="/",g_vm="details";   // folder entries + view mode (list/details/thumbs)
var IMG=/\.(jpg|jpeg|png|gif|bmp|webp)$/i,VID=/\.(mp4|mov|m4v|webm|mkv)$/i,AUD=/\.(mp3|m4a|flac|wav|aac|ogg)$/i,TXT=/\.(txt|json|csv|cfg|md|ino|h|c|log|xml|js)$/i,PDF=/\.pdf$/i,EPB=/\.epub$/i,WEBV=/\.(svg|html?)$/i;
var nbusy=0;
function busy(b){nbusy=Math.max(0,nbusy+(b?1:-1));document.getElementById("prog").className=nbusy?"on":""}
function F(uu,oo){busy(true);return fetch(uu,oo).catch(function(e){console.error("Fetch error",uu,e);busy(false);return Promise.reject(e)}).finally(function(){busy(false)})}
function ec(s){return s.replace(/'/g,"\\'")}
function at(s){return (s==null?"":(""+s)).replace(/&/g,"&amp;").replace(/"/g,"&quot;").replace(/</g,"&lt;")}
function u(p){return encodeURIComponent(p)}
function kind(n){if(IMG.test(n))return"i";if(VID.test(n))return"v";if(AUD.test(n))return"a";if(PDF.test(n))return"p";if(EPB.test(n))return"e";if(WEBV.test(n))return"w";if(TXT.test(n))return"t";return"o"}
function icon(n){if(IMG.test(n))return"🖼️";if(VID.test(n))return"🎬";if(AUD.test(n))return"🎵";if(PDF.test(n))return"📕";if(EPB.test(n))return"📖";if(WEBV.test(n))return"🌐";if(TXT.test(n))return"📄";return"📦"}
function fmt(b){if(b<1024)return b+" B";if(b<1048576)return (b/1024).toFixed(0)+" KB";return (b/1048576).toFixed(1)+" MB"}
function parent(d){return d.replace(/\/[^/]+\/?$/,"")||"/"}
function setbc(){var ps=cwd.split("/").filter(Boolean),h="<a onclick=\"ld('/')\">home</a>",a="";
 ps.forEach(function(p){a+="/"+p;h+=" / <a onclick=\"ld('"+ec(a)+"')\">"+p+"</a>"});document.getElementById("bc").innerHTML=h}
function ld(d){cwd=d;g_dir=d;setbc();
 F("/list?dir="+u(d)).then(function(r){return r.json()}).then(function(es){
  es.sort(function(a,b){return (b.dir-a.dir)||a.name.localeCompare(b.name)});
  g_ents=es;
  g_imgs=es.filter(function(e){return !e.dir&&IMG.test(e.name)}).map(function(e){return (d==="/"?"":d)+"/"+e.name});
  render();});}
function setVM(v){g_vm=v;try{localStorage.setItem("pcView",v)}catch(e){}render()}
function render(){
 var d=g_dir,es=g_ents,h="";
 ["L","D","T"].forEach(function(c,i){var b=document.getElementById("v"+c);if(b)b.className=(g_vm===["list","details","thumbs"][i]?"on":"")});
 var back=(d!=="/")?"<div class=row><div class=ic>⬅</div><div class=nm up onclick=\"ld('"+ec(parent(d))+"')\"><b>.. back</b></div></div>":"";
 if(g_vm==="thumbs"){
  h=back+"<div class=grid>";
  es.forEach(function(e){var p=(d==="/"?"":d)+"/"+e.name,k=kind(e.name);
   if(e.dir)h+="<div class=tile onclick=\"ld('"+ec(p)+"')\"><div class=ti2>📁</div><div class=nm2>"+e.name+"</div><button class=tmb onclick=\"event.stopPropagation();menu('"+ec(p)+"','d','"+ec(e.name)+"')\">⋯</button></div>";
   else if(k==="i")h+="<div class=ti><img loading=lazy src='/thumb?path="+u(p)+"' onclick=\"vw('"+ec(p)+"','i')\"><button class=tmb onclick=\"event.stopPropagation();menu('"+ec(p)+"','i','"+ec(e.name)+"')\">⋯</button></div>";
   else if(k==="v")h+="<div class='ti vthumb' onclick=\"prim('"+ec(p)+"','v')\"><img loading=lazy src='/thumb?path="+u(p)+"' onerror=\"this.remove()\"><span class=vbadge>▶</span><button class=tmb onclick=\"event.stopPropagation();menu('"+ec(p)+"','v','"+ec(e.name)+"')\">⋯</button></div>";
   else h+="<div class=tile onclick=\"prim('"+ec(p)+"','"+k+"')\"><div class=ti2>"+icon(e.name)+"</div><div class=nm2>"+e.name+"</div><button class=tmb onclick=\"event.stopPropagation();menu('"+ec(p)+"','"+k+"','"+ec(e.name)+"')\">⋯</button></div>";
  });
  h+="</div>";
 }else{
  h=back;
  es.forEach(function(e){var p=(d==="/"?"":d)+"/"+e.name,k=kind(e.name);
   if(e.dir)h+="<div class=row><div class=ic>📁</div><div class=nm onclick=\"ld('"+ec(p)+"')\"><b>"+e.name+"</b></div><button class=mb onclick=\"menu('"+ec(p)+"','d','"+ec(e.name)+"')\">⋯</button></div>";
   else{var sz=(g_vm==="details")?"<div class=sz>"+fmt(e.size)+"</div>":"";
    h+="<div class=row><div class=ic>"+icon(e.name)+"</div><div class=nm onclick=\"prim('"+ec(p)+"','"+k+"')\"><b>"+e.name+"</b>"+sz+"</div><button class=mb onclick=\"menu('"+ec(p)+"','"+k+"','"+ec(e.name)+"')\">⋯</button></div>";}
  });
 }
 if(!es.length)h+="<div class=muted>Empty folder</div>";
 document.getElementById("list").innerHTML=h;}
function prim(p,k){if(k==="t")ed(p);else if(k==="a")playAudio(p);else if(k==="e")openEpub(p);else if(k==="p"||k==="w")window.open("/dl?path="+u(p),"_blank");else if(k==="o")location.href="/dl?path="+u(p)+"&dl=1";else vw(p,k)}
function zipCwd(){location.href="/zip?dir="+u(cwd)}
// --- per-file action menu (delete is hidden until the menu is opened) ---
function menu(p,k,name){var h="<div class=h>"+name+"</div>";
 if(k==="d"){h+="<button class=it onclick=\"closeSheet();ld('"+ec(p)+"')\">📂 Open</button>";
  h+="<a class=it href=\"/zip?dir="+u(p)+"\" onclick=\"closeSheet()\">⬇ Download folder (ZIP)</a>";}
 if(k==="i")h+="<button class=it onclick=\"closeSheet();vw('"+ec(p)+"','i')\">👁 View</button>";
 if(k==="v")h+="<button class=it onclick=\"closeSheet();vw('"+ec(p)+"','v')\">▶ Play</button>";
 if(k==="a")h+="<button class=it onclick=\"closeSheet();playAudio('"+ec(p)+"')\">🎵 Play</button>";
 if(k==="p")h+="<button class=it onclick=\"closeSheet();window.open('/dl?path="+u(p)+"','_blank')\">📕 Open</button>";
 if(k==="e")h+="<button class=it onclick=\"closeSheet();openEpub('"+ec(p)+"')\">📖 Read</button>";
 if(k==="w"||k==="o")h+="<button class=it onclick=\"closeSheet();window.open('/dl?path="+u(p)+"','_blank')\">🌐 Open in browser</button>";
 if(k==="w")h+="<button class=it onclick=\"closeSheet();ed('"+ec(p)+"')\">✏️ Edit</button>";
 if(k==="t")h+="<button class=it onclick=\"closeSheet();ed('"+ec(p)+"')\">✏️ Edit</button>";
 if(k!=="d")h+="<a class=it href=\"/dl?path="+u(p)+"&dl=1\" onclick=\"closeSheet()\">⬇ Download</a>";
 h+="<button class='it d' onclick=\"closeSheet();rm('"+ec(p)+"',"+(k==="d"?1:0)+")\">🗑️ Delete</button>";
 document.getElementById("spanel").innerHTML=h;document.getElementById("sheet").style.display="block";}
function closeSheet(){document.getElementById("sheet").style.display="none"}
function rm(p,d){if(!confirm(d?("Delete folder «"+p+"» and ALL its contents?"):("Delete "+p+"?")))return;F("/rm?path="+u(p)).then(function(){ld(cwd)})}
function mk(){var n=prompt("New folder name:");if(!n||!(n=n.trim()))return;F("/mkdir?path="+u((cwd==="/"?"":cwd)+"/"+n)).then(function(){ld(cwd)})}
function up(){var fi=document.getElementById("f"),f=fi.files[0];if(!f)return;var fd=new FormData();fd.append("f",f);F("/up?dir="+u(cwd),{method:"POST",body:fd}).then(function(){fi.value="";ld(cwd)})}
function arrows(){var s=(g_isImg&&g_imgs.length>1)?"block":"none";document.getElementById("vprev").style.display=s;document.getElementById("vnext").style.display=s}
function showImg(){var p=g_imgs[g_ii],vc=document.getElementById("vc");
 vc.innerHTML="<div class=muted>Loading…</div>";
 var im=new Image();
 im.onload=function(){if(g_imgs[g_ii]===p){vc.innerHTML="";vc.appendChild(im)}};
 im.onerror=function(){if(g_imgs[g_ii]===p)vc.innerHTML="<div class=muted>Load error</div>"};
 im.src="/dl?path="+u(p);
 document.getElementById("vtt").textContent=p.split("/").pop()+"  ("+(g_ii+1)+"/"+g_imgs.length+")";arrows()}
function navImg(d){if(!g_isImg||g_imgs.length<2)return;g_ii=(g_ii+d+g_imgs.length)%g_imgs.length;showImg()}
function vw(p,t){var vwr=document.getElementById("vwr"),vc=document.getElementById("vc");
 if(t==="i"){g_isImg=true;g_ii=g_imgs.indexOf(p);if(g_ii<0){g_imgs=[p];g_ii=0}showImg();vwr.style.display="flex";return}
 g_isImg=false;vc.innerHTML="<video src='/dl?path="+u(p)+"' controls autoplay playsinline></video>";
 document.getElementById("vtt").textContent=p.split("/").pop();arrows();vwr.style.display="flex"}
function cv(){var vc=document.getElementById("vc");g_isImg=false;arrows();vc.innerHTML="";document.getElementById("vwr").style.display="none"}
// mini audio player (bottom bar; keeps playing while you browse)
function playAudio(p){var a=document.getElementById("pau");a.src="/dl?path="+u(p);
 document.getElementById("ptt").textContent="🎵 "+p.split("/").pop();document.getElementById("player").classList.add("on");a.play()}
function pstop(){var a=document.getElementById("pau");a.pause();a.removeAttribute("src");a.load();document.getElementById("player").classList.remove("on")}
// --- EPUB reader (epub.js + jszip served from /lib on SD; also works offline) ---
var g_epbook=null,g_eprend=null;
function loadJS(src){return new Promise(function(res,rej){
 if(document.querySelector("script[data-s='"+src+"']"))return res();
 var s=document.createElement("script");s.src=src;s.dataset.s=src;s.onload=res;s.onerror=function(){rej(src)};document.head.appendChild(s)})}
function openEpub(p){var ov=document.getElementById("epov"),ar=document.getElementById("eparea");
 document.getElementById("eptt").textContent="📖 "+p.split("/").pop();
 ar.innerHTML="<div class=muted>Loading reader…</div>";ov.style.display="flex";busy(true);
 loadJS("/dl?path="+u("/lib/jszip.min.js")).then(function(){return loadJS("/dl?path="+u("/lib/epub.min.js"))}).then(function(){
  ar.innerHTML="";
  g_epbook=ePub("/dl?path="+u(p),{openAs:"epub"});   // URL does not end in .epub: force the type explicitly
  g_eprend=g_epbook.renderTo("eparea",{width:"100%",height:"100%",flow:"paginated",spread:"none"});
  return g_eprend.display();
 }).then(function(){busy(false)}).catch(function(e){busy(false);
  ar.innerHTML="<div class=muted style='padding:24px'>EPUB reader not found.<br><br>Copy <b>jszip.min.js</b> and <b>epub.min.js</b> in a <b>/lib</b> folder on the microSD (found in the <i>tools/sdcard/lib</i> folder of the project).<br><br><small>"+e+"</small></div>"})}
function epClose(){document.getElementById("epov").style.display="none";document.getElementById("eparea").innerHTML="";
 if(g_eprend){try{g_eprend.destroy()}catch(e){}}if(g_epbook){try{g_epbook.destroy()}catch(e){}}g_eprend=g_epbook=null}
function epPrev(){if(g_eprend)g_eprend.prev()}
function epNext(){if(g_eprend)g_eprend.next()}
function theme(){document.body.classList.toggle("light");var l=document.body.classList.contains("light");
 try{localStorage.setItem("pcTheme",l?"l":"d")}catch(e){}document.getElementById("thb").textContent=l?"☀️":"🌙"}
// --- plain-text / raw JSON editor ---
var edPath="";
function setst(id,m,ok){var e=document.getElementById(id);e.textContent=m||"";e.style.color=ok?"#22c55e":"#ef4444"}
function ed(p){edPath=p;document.getElementById("ettl").textContent=p;setst("est","");
 var ta=document.getElementById("ta");ta.value="Loading…";document.getElementById("ed").style.display="flex";
 F("/dl?path="+u(p)).then(function(r){return r.text()}).then(function(t){
  if(p==="/config.json"){try{t=JSON.stringify(JSON.parse(t),null,2)}catch(e){}}
  ta.value=t}).catch(function(){ta.value="(load error)"})}
function sv(){var t=document.getElementById("ta").value;
 if(edPath==="/config.json"){try{JSON.parse(t)}catch(e){setst("est","Invalid JSON: "+e.message,false);return}}
 var fd=new FormData();fd.append("path",edPath);fd.append("data",t);
 F("/save",{method:"POST",body:fd}).then(function(){setst("est",edPath==="/config.json"?"Saved — restart to apply":"Saved",true);ld(cwd)})}
function ce(){document.getElementById("ed").style.display="none"}
// --- GRAPHICAL CONFIG (reads/writes config.json) ---
var g_cfg={};
function wrow(s,p){return "<div class=subrow><input class=wssid placeholder='SSID' value=\""+at(s)+"\"><input class=wpass placeholder='Password' value=\""+at(p)+"\"><button class='btn d' onclick=\"this.closest('.subrow').remove()\">✕</button></div>"}
function lrow(n,la,lo){return "<div class=subrow><input class=lname placeholder='Name' value=\""+at(n)+"\"><input class=lmun placeholder='Lat' value=\""+at(la)+"\"><input class=lest placeholder='Lon' value=\""+at(lo)+"\"><button class='btn d' onclick=\"this.closest('.subrow').remove()\">✕</button></div>"}
function addWifi(){document.getElementById("cfWifi").insertAdjacentHTML("beforeend",wrow("",""))}
function addLoc(){document.getElementById("cfLocs").insertAdjacentHTML("beforeend",lrow("","",""))}
function fld(id,lab,v,t){return "<div class=fld><label>"+lab+"</label><input type='"+(t||"text")+"' id='"+id+"' value=\""+at(v)+"\"></div>"}
function chk(id,lab,v){return "<div class=chk><input type=checkbox id='"+id+"' "+(v?"checked":"")+"><label for='"+id+"'>"+lab+"</label></div>"}
function cfgOpen(){setst("cfst","loading...",true);
 F("/dl?path="+u("/config.json")).then(function(r){return r.text()}).then(function(t){
  try{g_cfg=JSON.parse(t)}catch(e){setst("cfst","config.json invalid; use valid { } JSON",false);g_cfg={}}
  var m=g_cfg.modos||{},w=g_cfg.wifi_modo||{};
  var h="";
  h+="<div class=sec><h3>WiFi (redes)</h3><div id=cfWifi></div><button class=btn onclick='addWifi()'>+ network</button></div>";
  h+="<div class=sec><h3>Weather locations</h3>"+fld("c_key","API key (unused)",g_cfg.aemet_api_key||"")+"<div id=cfLocs></div><button class=btn onclick='addLoc()'>+ location</button></div>";
  h+="<div class=sec><h3>General</h3>"+fld("c_tz","Timezone (TZ string)",g_cfg.timezone||"")+fld("c_car","Seconds between photos",g_cfg.carousel_seconds||300,"number")+chk("c_rot","Auto-rotate photos",g_cfg.photo_auto_rotate!==false)+fld("c_deep","Inactivity sleep (minutes)",g_cfg.deep_sleep_minutes||60,"number")+"</div>";
  h+="<div class=sec><h3>Modos activos</h3>"+chk("m_clima","Weather",m.clima!==false)+chk("m_carrusel","Carousel",m.carrusel!==false)+chk("m_musica","Music",m.musica!==false)+chk("m_libro","Books",m.libro!==false)+chk("m_wifi","WiFi",m.wifi!==false)+"</div>";
  h+="<div class=sec><h3>Folders &amp; fonts</h3>"+fld("c_fotos","Photos folder",g_cfg.fotos_dir||"/Fotos")+fld("c_mus","Music folder",g_cfg.musica_dir||"/Music")+fld("c_lib","Books folder",g_cfg.libros_dir||"/Library")+fld("c_ft","Title font path",g_cfg.font_title||"/fonts/title.vlw")+fld("c_fb","Body font path",g_cfg.font_body||"/fonts/body.vlw")+"</div>";
  h+="<div class=sec><h3>Wi-Fi Mode (this manager)</h3>"+fld("c_ap","AP SSID",w.ap_ssid||"PaperColor")+fld("c_app","AP password (min 8 chars)",w.ap_pass||"")+fld("c_wu","Web username",w.user||"admin")+fld("c_wp","Web password",w.pass||"")+"</div>";
  document.getElementById("cfgbd").innerHTML=h;
  (g_cfg.wifi||[]).forEach(function(x){document.getElementById("cfWifi").insertAdjacentHTML("beforeend",wrow(x.ssid,x.pass))});
  if(!(g_cfg.wifi||[]).length)addWifi();
  (g_cfg.locations||[]).forEach(function(x){document.getElementById("cfLocs").insertAdjacentHTML("beforeend",lrow(x.name,x.lat,x.lon))});
  setst("cfst","");document.getElementById("cfg").style.display="flex";})}
function val(id){return document.getElementById(id).value}
function cfgSave(){
 var o=g_cfg||{};
 o.wifi=[];document.querySelectorAll("#cfWifi .subrow").forEach(function(r){var s=r.querySelector(".wssid").value.trim();if(s)o.wifi.push({ssid:s,pass:r.querySelector(".wpass").value})});
 o._unused=val("c_key");o.timezone=val("c_tz");
 o.carousel_seconds=parseInt(val("c_car"))||300;o.photo_auto_rotate=document.getElementById("c_rot").checked;
 o.deep_sleep_minutes=parseInt(val("c_deep"))||60;
 o.modos={clima:document.getElementById("m_clima").checked,carrusel:document.getElementById("m_carrusel").checked,musica:document.getElementById("m_musica").checked,libro:document.getElementById("m_libro").checked,wifi:document.getElementById("m_wifi").checked};
 o.fotos_dir=val("c_fotos");o.musica_dir=val("c_mus");o.libros_dir=val("c_lib");o.font_title=val("c_ft");o.font_body=val("c_fb");
 o.wifi_modo={ap_ssid:val("c_ap"),ap_pass:val("c_app"),user:val("c_wu"),pass:val("c_wp")};
 o.locations=[];document.querySelectorAll("#cfLocs .subrow").forEach(function(r){var n=r.querySelector(".lname").value.trim();if(n)o.locations.push({name:n,lat:parseFloat(r.querySelector(".lmun").value)||0,lon:parseFloat(r.querySelector(".lest").value)||0})});
 var fd=new FormData();fd.append("path","/config.json");fd.append("data",JSON.stringify(o,null,2));
 F("/save",{method:"POST",body:fd}).then(function(){setst("cfst","Saved — restart to apply",true)})}
function cfgRaw(){cfgClose();ed("/config.json")}
function cfgClose(){document.getElementById("cfg").style.display="none"}
// saved theme + viewer gestures (swipe on mobile) + keyboard (arrows / Esc)
(function(){try{if(localStorage.getItem("pcTheme")==="l")document.body.classList.add("light");
  var v=localStorage.getItem("pcView");if(v)g_vm=v}catch(e){}
 document.getElementById("thb").textContent=document.body.classList.contains("light")?"☀️":"🌙";
 var sx=0,vc=document.getElementById("vc");
 vc.addEventListener("touchstart",function(e){sx=e.changedTouches[0].clientX},{passive:true});
 vc.addEventListener("touchend",function(e){var dx=e.changedTouches[0].clientX-sx;if(g_isImg&&Math.abs(dx)>40)navImg(dx<0?1:-1)},{passive:true});
 document.addEventListener("keydown",function(e){
  if(document.getElementById("epov").style.display==="flex"){
   if(e.key==="ArrowLeft")epPrev();else if(e.key==="ArrowRight")epNext();else if(e.key==="Escape")epClose();return}
  if(document.getElementById("vwr").style.display!=="flex")return;
  if(e.key==="ArrowLeft")navImg(-1);else if(e.key==="ArrowRight")navImg(1);else if(e.key==="Escape")cv()});
 var ex=0,ea=document.getElementById("eparea");
 ea.addEventListener("touchstart",function(e){ex=e.changedTouches[0].clientX},{passive:true});
 ea.addEventListener("touchend",function(e){var dx=e.changedTouches[0].clientX-ex;if(Math.abs(dx)>40)(dx<0?epNext:epPrev)()},{passive:true});
})();
ld("/");
</script></body></html>
)HTMLPAGE";
