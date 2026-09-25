(() => {
'use strict';var h,aa=typeof Object.defineProperties=="function"?Object.defineProperty:function(a,b,c){if(a==Array.prototype||a==Object.prototype)return a;a[b]=c.value;return a},ba=function(a){a=["object"==typeof globalThis&&globalThis,a,"object"==typeof window&&window,"object"==typeof self&&self,"object"==typeof global&&global];for(var b=0;b<a.length;++b){var c=a[b];if(c&&c.Math==Math)return c}throw Error("Cannot find global object");},ca=ba(this),k=function(a,b){if(b)a:{var c=ca;a=a.split(".");
for(var d=0;d<a.length-1;d++){var e=a[d];if(!(e in c))break a;c=c[e]}a=a[a.length-1];d=c[a];b=b(d);b!=d&&b!=null&&aa(c,a,{configurable:!0,writable:!0,value:b})}},m=function(a){function b(d){return a.next(d)}function c(d){return a.throw(d)}return new Promise(function(d,e){function f(g){g.done?d(g.value):Promise.resolve(g.value).then(b,c).then(f,e)}f(a.next())})};k("Symbol.dispose",function(a){return a?a:Symbol("Symbol.dispose")});
k("Object.entries",function(a){return a?a:function(b){var c=[],d;for(d in b)Object.prototype.hasOwnProperty.call(b,d)&&c.push([d,b[d]]);return c}});k("Object.values",function(a){return a?a:function(b){var c=[],d;for(d in b)Object.prototype.hasOwnProperty.call(b,d)&&c.push(b[d]);return c}});
var da=function(a,b){a instanceof String&&(a+="");var c=0,d=!1,e={next:function(){if(!d&&c<a.length){var f=c++;return{value:b(f,a[f]),done:!1}}d=!0;return{done:!0,value:void 0}}};e[Symbol.iterator]=function(){return e};return e};k("Array.prototype.values",function(a){return a?a:function(){return da(this,function(b,c){return c})}});
k("Array.prototype.includes",function(a){return a?a:function(b,c){var d=this;d instanceof String&&(d=String(d));var e=d.length;c=c||0;for(c<0&&(c=Math.max(c+e,0));c<e;c++){var f=d[c];if(f===b||Object.is(f,b))return!0}return!1}});

var n=this||self,p=function(a){var b=typeof a;return b=="object"&&a!=null||b=="function"},ea=function(a,b,c){return a.call.apply(a.bind,arguments)},fa=function(a,b,c){if(!a)throw Error();if(arguments.length>2){var d=Array.prototype.slice.call(arguments,2);return function(){var e=Array.prototype.slice.call(arguments);Array.prototype.unshift.apply(e,d);return a.apply(b,e)}}return function(){return a.apply(b,arguments)}},q=function(a,b,c){q=Function.prototype.bind&&Function.prototype.bind.toString().indexOf("native code")!=
-1?ea:fa;return q.apply(null,arguments)},r=function(a,b){function c(){}c.prototype=b.prototype;a.B=b.prototype;a.prototype=new c;a.prototype.constructor=a;a.Da=function(d,e,f){for(var g=Array(arguments.length-2),l=2;l<arguments.length;l++)g[l-2]=arguments[l];return b.prototype[e].apply(d,g)}};var ma=function(a){switch(a){case ha.name:return ha;case t.name:return t;case ia.name:return ia;case ja.name:return ja;case ka.name:return ka;default:let b=la.get(a);b||(b=new u(a),la.set(a,b));return b}},u=class{constructor(a){this.name=a}toString(){return this.name}},la=new Map,ha=new u("async"),t=new u("generic"),ia=new u("runtime"),ja=new u("system"),ka=new u("crypto"),v=class extends Error{constructor(a,b,c){super(c);this.code=a;this.category=b;Object.setPrototypeOf(this,new.target.prototype)}toString(){return`${this.category}:${this.code} (${this.message||
"Undefined error."})`}};function w(a){throw new v(416,t,a);};var na=class{constructor(){let a=void 0,b=void 0;this.promise=new Promise((c,d)=>{a=c;b=d});this.resolve=a;this.reject=b}},x=class extends v{constructor(a,b="One or more errors occurred."){super(1,ha,b);this.errors=a;Object.setPrototypeOf(this,new.target.prototype)}flatten(){const a=[],b=[this.errors.slice(0)];for(;b.length!==0;){const c=b[0];for(;c.length!==0;){const d=c.shift();if(d instanceof x){b.unshift(d.errors.slice(0));break}else a.push(d)}b[0]===c&&b.shift()}return new x(a,this.message)}};var oa=class{constructor(){this.i=[]}subscribe(a){this.i.push(a)}unsubscribe(a){a=this.i.indexOf(a);if(a===-1)return!1;this.i.splice(a,1);return!0}publish(...a){const b=[],c=this.i.slice(0);for(const d of c)b.push(d(...a));return b}};const pa=Array.prototype.indexOf?function(a,b){return Array.prototype.indexOf.call(a,b,void 0)}:function(a,b){if(typeof a==="string")return typeof b!=="string"||b.length!=1?-1:a.indexOf(b,0);for(let c=0;c<a.length;c++)if(c in a&&a[c]===b)return c;return-1};function y(){this.ra=Date.now()}var qa=null;y.prototype.set=function(a){this.ra=a};y.prototype.reset=function(){this.set(Date.now())};y.prototype.get=function(){return this.ra};var z=class{constructor(a,b){this.name=a;this.value=b}toString(){return this.name}},A=new z("OFF",Infinity),ra=new z("SEVERE",1E3),B=new z("WARNING",900),sa=new z("INFO",800),ta=new z("CONFIG",700),ua=new z("ALL",0),va=[A,new z("SHOUT",1200),ra,B,sa,ta,new z("FINE",500),new z("FINER",400),new z("FINEST",300),ua],wa=class{constructor(){this.O=0;this.clear()}clear(){this.ga=Array(this.O);this.ha=-1;this.ia=!1}},xa,za=class{constructor(a,b,c){this.R=void 0;this.reset(a||A,b,c,void 0,void 0)}reset(a,
b,c,d,e){this.ca=d||Date.now();this.S=a;this.ya=b;this.Z=c;this.R=void 0;this.Aa=typeof e==="number"?e:ya}getMessage(){return this.ya}},ya=0,Aa=function(a){return a.level?a.level:a.parent?Aa(a.parent):A},Ba=class{constructor(a,b=null){this.level=null;this.i=[];this.parent=b||null;this.children=[];this.ka={getName:()=>a}}publish(a){let b=this;for(;b;)b.i.forEach(c=>{c(a)}),b=b.parent}},C=function(a,b){var c=a.entries[b];if(c)return c;c=b.lastIndexOf(".");c=b.slice(0,Math.max(c,0));c=C(a,c);const d=
new Ba(b,c);a.entries[b]=d;c.children.push(d);return d},Ca=class{constructor(){this.entries={};const a=new Ba("");a.level=ta;this.entries[""]=a}},Da,D=function(){Da||(Da=new Ca);return Da},Fa=function(a){var b=Ea;b&&C(D(),b.getName()).i.push(a)},Ga=function(){var a=Ea;a&&(C(D(),a.getName()).level=ua)},E=function(a,b,c){var d;if(d=a)if(d=a&&b){d=b.value;var e=a?Aa(C(D(),a.getName())):A;d=d>=e.value}if(d){b=b||A;d=C(D(),a.getName());typeof c==="function"&&(c=c());xa||(xa=new wa);e=xa;a=a.getName();
if(e.O>0){var f=(e.ha+1)%e.O;e.ha=f;e.ia?(e=e.ga[f],e.reset(b,c,a),a=e):(e.ia=f==e.O-1,a=e.ga[f]=new za(b,c,a))}else a=new za(b,c,a);a.R=void 0;d.publish(a)}};

function F(a){return a.toString().indexOf("`")===-1}F(a=>a``)||F(a=>a`\0`)||F(a=>a`\n`)||F(a=>a`\u0000`);"ARTICLE SECTION NAV ASIDE H1 H2 H3 H4 H5 H6 HEADER FOOTER ADDRESS P HR PRE BLOCKQUOTE OL UL LH LI DL DT DD FIGURE FIGCAPTION MAIN DIV EM STRONG SMALL S CITE Q DFN ABBR RUBY RB RT RTC RP DATA TIME CODE VAR SAMP KBD SUB SUP I B U MARK BDI BDO SPAN BR WBR NOBR INS DEL PICTURE PARAM TRACK MAP TABLE CAPTION COLGROUP COL TBODY THEAD TFOOT TR TD TH SELECT DATALIST OPTGROUP OPTION OUTPUT PROGRESS METER FIELDSET LEGEND DETAILS SUMMARY MENU DIALOG SLOT CANVAS FONT CENTER ACRONYM BASEFONT BIG DIR HGROUP STRIKE TT".split(" ").concat(["BUTTON",
"INPUT"]);var Ha=function(a){this.za=a||"";qa||(qa=new y);this.Ca=qa};h=Ha.prototype;h.fa=!0;h.ba=!0;h.ta=!0;h.Ba=!0;h.sa=!1;h.ua=!1;var G=function(a){return a<10?"0"+a:String(a)},Ia=function(a){Ha.call(this,a)};r(Ia,Ha);
var Ja=function(a,b){const c=[];c.push(a.za," ");if(a.ba){var d=new Date(b.ca);c.push("[",G(d.getFullYear()-2E3)+G(d.getMonth()+1)+G(d.getDate())+" "+G(d.getHours())+":"+G(d.getMinutes())+":"+G(d.getSeconds())+"."+G(Math.floor(d.getMilliseconds()/10)),"] ")}if(a.ta){d=c.push;var e=a.Ca.get();e=(b.ca-e)/1E3;let f=e.toFixed(3),g=0;if(e<1)g=2;else for(;e<100;)g++,e*=10;for(;g-- >0;)f=" "+f;d.call(c,"[",f,"s] ")}a.Ba&&c.push("[",b.Z,"] ");a.ua&&c.push("[",b.S.name,"] ");c.push(b.getMessage());a.sa&&(b=
b.R,b!==void 0&&c.push("\n",b instanceof Error?b.message:String(b)));a.fa&&c.push("\n");return c.join("")};function Ka(a=new Ia){a.ba=!0;a.ta=!1;a.ua=!0}var Na=class{constructor(a){this.name=a;this.target=C(D(),a).ka}log(a,b){E(this.target,a!=null?a:La,b)}error(a){E(this.target,ra,a)}warn(a){E(this.target,B,a)}info(a){E(this.target,sa,a)}debug(a){E(this.target,La,a)}trace(a){E(this.target,Ma,a)}},Ea=C(D(),"").ka,La=new z("DEBUG",600),Ma=new z("TRACE",100);va.push(La);va.push(Ma);

// RPC sender — MessageServiceHost envelope for any component extension.
// Magic is packed-ASCII SECRPC (or another 6-byte key from RBIComponentRPC).
function rbiPackedMagic(id){
    try{var r=typeof self!=="undefined"?self:typeof window!=="undefined"?window:null;
      if(r&&r.RBIComponentRPC&&r.RBIComponentRPC.magic)return r.RBIComponentRPC.magic(id);
      if(r&&r.__RBI_SECRPC__)return r.__RBI_SECRPC__;}catch(x){}
    return 91556947316803;}
function rbiPackedOurs(m){
    if(m===91556947316803)return true;
    try{var r=typeof self!=="undefined"?self:typeof window!=="undefined"?window:null;
      if(r&&r.RBIComponentRPC&&r.RBIComponentRPC.accepts(m))return true;
      if(r&&r.RBIEvMojo&&r.RBIEvMojo.accepts(m))return true;}catch(x){}
    return false;}
function rbiRPCMethods(){
    try{var r=typeof self!=="undefined"?self:typeof window!=="undefined"?window:null;
      if(r&&r.RBIComponentRPC&&r.RBIComponentRPC.METHODS)return r.RBIComponentRPC.METHODS.slice()}catch(x){}
    return "getAccounts hello getLogBuffer recordLog getQuirks updateQuirks uiEvent proxyUIEvent getProxyConfig".split(" ");}
function Oa(a,b,...c){
    return m(function*(){
        const d=new na, e={magic:rbiPackedMagic(), host:a!==null&&typeof a==="number"?"*":a, method:b, args:c};
        function pickSwTarget(){
            if(typeof navigator==="undefined"||!navigator.serviceWorker)return null;
            if(navigator.serviceWorker.controller)return navigator.serviceWorker.controller;
            const host=typeof window!=="undefined"?window:typeof self!=="undefined"?self:null;
            const reg=host&&host.__RBI_EV_SW_REGISTRATION__;
            if(reg&&reg.active)return reg.active;
            if(reg&&reg.waiting){try{reg.waiting.postMessage({type:"SKIP_WAITING"})}catch(x){}return reg.waiting}
            if(reg&&reg.installing)return reg.installing;
            return null}
        function trySwPost(){
            return new Promise((resolve,reject)=>{
                const target=pickSwTarget();
                if(!target){reject(new Error("Service Worker controller unavailable"));return}
                const channel=new MessageChannel();
                let settled=!1;
                const swBudget=b==="hello"?45000:25000;
                const timer=setTimeout(()=>{if(settled)return;settled=!0;reject(new Error("SW RPC stall"))},swBudget);
                channel.port1.onmessage=(event)=>{if(settled)return;settled=!0;clearTimeout(timer);
                    const g=event.data;g.error!==void 0?reject(Pa(g.error)):resolve(g.result!==void 0?g.result:g)};
                try{target.postMessage(e,[channel.port2])}catch(err){
                    if(!settled){settled=!0;clearTimeout(timer);reject(err)}}})}
        function trySend(id){
            const g=typeof window!=="undefined"?window:typeof self!=="undefined"?self:null;
            const send=g&&g.__RBI_NATIVE_CHROME__&&g.__RBI_NATIVE_CHROME__.runtime&&g.__RBI_NATIVE_CHROME__.runtime.sendMessage;
            if(!send||!send.__RBI_NATIVE_SEND_MESSAGE__)return Promise.reject(new Error("native sendMessage unavailable"));
            return new Promise((resolve,reject)=>{function onResp(h){const j=chrome.runtime.lastError;
            if(j){reject(new Error(j.message));return}if(h&&h.error!==void 0)reject(Pa(h.error));
            else resolve(h&&h.result!==void 0?h.result:h)}
            try{if(id)send(id,e,onResp);else send(e,onResp)}catch(k){reject(k)}})}
        function sendToTab(tabId){
            return new Promise((resolve,reject)=>{function onResp(h){const j=chrome.runtime.lastError;
            if(j){reject(new Error(j.message));return}if(h&&h.error!==void 0)reject(Pa(h.error));
            else resolve(h&&h.result!==void 0?h.result:h)}
    try{chrome.tabs.sendMessage(tabId,e,onResp)}catch(k){reject(k)}})}
        function tryParentRelay(budget){
            return new Promise((resolve,reject)=>{const g=typeof window!=="undefined"?window:null;
            if(!g||!g.parent||g.parent===g){reject(new Error("parent relay unavailable"));return}
            const id="relay-"+Math.random().toString(36).slice(2);let settled=!1;
            const timer=setTimeout(()=>{if(settled)return;settled=!0;g.removeEventListener("message",onRelay);
            reject(new Error("parent EV relay timeout for "+b))},budget||45000);
            function onRelay(ev){const d=ev.data||{};
            if(d.channel!=="rbi-ev-offscreen"||d.type!=="rpc-relay-result"||d.id!==id)return;
            settled=!0;clearTimeout(timer);g.removeEventListener("message",onRelay);
            if(d.ok)resolve(d.result);else reject(new Error(d.error||"parent relay failed"))}
            g.addEventListener("message",onRelay);
            try{g.parent.postMessage({channel:"rbi-ev-offscreen",type:"rpc-relay-request",id:id,method:e.method,args:e.args||[],timeoutMs:budget},location.origin||"*")}
            catch(err){settled=!0;clearTimeout(timer);g.removeEventListener("message",onRelay);reject(err)}})}
        const delays=[0,400,1200,2500,5000,9000];let lastErr=null;
        const g=typeof window!=="undefined"?window:typeof self!=="undefined"?self:null;
        const inChildFrame=g&&g.parent&&g.parent!==g;
        const skipParentRelay=!!Oa.__skipParentRelay;
        const skipSwRelay=!!Oa.__skipSwRelay;
        const pg=g&&g.parent&&g.parent!==g?g.parent:null;
        const portalMode=!!((g&&(g.__RBI_EV_COMPONENT__||g.__RBI_EV_LOG_CONSOLE_PAGE__||g.__RBI_EV_LOG_SERVICE__||g.__RBI_GOOGLE_EV_PRESENT__===false))||(pg&&(pg.__RBI_EV_COMPONENT__||pg.__RBI_EV_LOG_CONSOLE_PAGE__||pg.__RBI_EV_LOG_SERVICE__||pg.__RBI_GOOGLE_EV_PRESENT__===false)));
        if(portalMode&&g&&g.__RBI_EV_SW_READY__){try{yield g.__RBI_EV_SW_READY__}catch(x){}}
        for(let attempt=0;attempt<delays.length;attempt++){
            if(delays[attempt]>0)yield new Promise(r=>setTimeout(r,delays[attempt]));
            if(portalMode&&!skipSwRelay){
                try{
                    if(typeof navigator!=="undefined"&&navigator.serviceWorker){
                        d.resolve(yield trySwPost());return d.promise}
                }catch(err){lastErr=err}
            }
            if(!portalMode){
            try{
                if(typeof chrome!=="undefined"&&chrome.runtime){
                    if(chrome.runtime.id){d.resolve(yield trySend(null));return d.promise}
                    const ids=[];
                    if(g&&g.__RBI_EV_EXTENSION_ID__)ids.push(g.__RBI_EV_EXTENSION_ID__);
                    ids.push("callobklhcbilhphinckomhgkigmfocg");
                    const seen={};
                    for(const id of ids){if(!id||seen[id])continue;seen[id]=!0;
                        try{d.resolve(yield trySend(id));return d.promise}catch(err){lastErr=err}}
                    try{d.resolve(yield trySend(null));return d.promise}catch(err){lastErr=err}
                }
            }catch(err){lastErr=err}
            }
            if(!portalMode){
                try{
                    if(typeof navigator!=="undefined"&&navigator.serviceWorker&&navigator.serviceWorker.controller){
                        d.resolve(yield trySwPost());return d.promise}
                }catch(err){lastErr=err}
            }
            try{
                if(inChildFrame&&!skipParentRelay&&!portalMode){
                    d.resolve(yield tryParentRelay(45000));return d.promise}
            }catch(err){lastErr=err}
        }
        function __rbiEvStub(m,a){if(m==="hello")return{ok:!0,path:"portal_stub"};if(m==="getQuirks")return{simulated:!0,path:"portal_stub"};if(m==="recordLog")return{ok:!0,stub:!0};if(m==="getLogBuffer")return[];if(m==="getAccounts")return[];return{ok:!0,stub:!0,method:m}}
        try{fetch("/api/rbi/ev-rpc",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({method:b,args:c||[],source:"offscreen_script"})}).then(function(r){return r.json()}).then(function(j){if(j&&j.result!==void 0)d.resolve(j.result);else d.resolve(__rbiEvStub(b,c))}).catch(function(){d.resolve(__rbiEvStub(b,c))})}catch(x){d.resolve(__rbiEvStub(b,c))}
        return d.promise;
    }())
}

function H(a){return a instanceof Error?a instanceof x?{type:3,errors:a.errors.map(H),message:a.message}:a instanceof v?{type:2,code:a.code,category:a.category.name,message:a.message}:{type:1,message:a.message||"Undefined error."}:{type:0,object:a}}function Pa(a){switch(a.type){case 0:return a.object;case 1:return Error(a.message);case 2:return new v(a.code,ma(a.category),a.message);case 3:return new x(a.errors.map(Pa),a.message)}};var Qa=class{constructor(){this.host="*";this.events=new Map;this.qa=new Map}invoke(a,...b){const c=this;return m(function*(){return Oa(c.host,a,...b)}())}};function Ra(a){function b(d){if(!d||d.length===0)return!1;var e=d.charCodeAt(0);if(!(e>=65&&e<=90||e>=97&&e<=122)&&e!==36&&e!==95)return!1;for(e=1;e<d.length;e++){const f=d.charCodeAt(e);if(!(f>=65&&f<=90||f>=97&&f<=122||f>=48&&f<=57)&&f!==36&&f!==95)return!1}return!0}const c=d=>{switch(typeof d){case "object":return d===null?"null":Array.isArray(d)?`[${d.map(c).join(", ")}]`:`{${Object.entries(d).map(([f,g])=>`${b(f)?f:`'${f}'`}: ${c(g)}`).join(", ")}}`;case "string":let e="";for(let f=0;f<d.length;f++){const g=
d[f];switch(g){case "\\":e+="\\\\";break;case "'":e+="\\'";break;case '"':e+='\\"';break;default:e+=g}}return`'${e}'`;case "function":return"function";case "undefined":return"undefined";default:return String(d)}};return c(a)};var Sa=function(a,b){a.log.trace(()=>`New connection to '${b.name}'.`);const c=a.events.get(b.name);if(c){var d=c.J,e={port:b,H:()=>{a.log.trace(()=>`Data query for '${b.name}'.`);b.postMessage(c.data)},na:()=>{const f=d.findIndex(g=>g.port===b);f!==-1&&(a.log.trace(()=>`Port '${b.name}' disconnected.`),d.splice(f,1))}};b.onmessage = e.H; b.onmessageerror = e.na;d.push(e)}},Ta=function(a,b){a.v&&w(`Host ${a.name} is already open.`);a.i.fetch&&w("Handler for 'fetch' is already registered.");
a.log.trace(()=>"Registering 'fetch' handler.");a.i.fetch=b},Ua=function(a,b,c){a.v&&w(`Host ${a.name} is already open.`);a.events.get(b)&&w(`Event ${b} is already exposed.`);a.log.trace(()=>`Exposing event '${b}'.`);const d={event:c,u:(...e)=>{d.data=e;if(!d.Y){d.Y=!0;try{for(const f of d.J)try{f.port.postMessage(e)}catch(g){a.log.warn(`Post message to '${f.port.name}' failed. ${g.message}`)}}finally{d.Y=!1}}},J:[],Y:!1,data:[]};a.events.set(b,d)},Wa=function(a,b,c,d,e){if(c){a.log.trace(()=>`Handling '${a.name}:${String(d.method)}' from ${Ra(b)}...`);
try{const g=c(...d.args,b);if(g instanceof Promise)return Va(g,e),!0;var f={result:g}}catch(g){f={error:H(g)}}e(f);return!1}if(d.host!=="*"&&a.name!=="*"&&d.host===a.name)return a.log.error(`No handler for '${a.name}:${String(d.method)}'.`),c={error:H(new v(1,t,`No handler for '${a.name}:${String(d.method)}'.`))},e(c),!1},Va=function(a,b){m(function*(){let c;try{c={result:yield a}}catch(d){c={error:H(d)}}b(c)}())},

// Updated Receiver Host Class bound to standard Web API Message Events
Xa=class{
    constructor(){
        this.name="*";
        this.U=void 0;
        this.i={};
        this.events=new Map;
        this.v=!1;
        this.log=new Na(`MessageServiceHost(${this.name})`);
        
        this.H = (event) => {
            const source = event.source;
            const data = event.data;
            const sendResponse = (response) => {
                if (event.ports && event.ports[0]) {
                    event.ports[0].postMessage(response);
                }
            };
            this.onRequest(source, data, sendResponse);
        };
    }
    
    open(){
        const a=this;return m(function*(){
            a.v&&w(`Host ${a.name} is already open.`);
            a.log.trace("Opening...");
            
            if(typeof navigator!=='undefined' && navigator.serviceWorker){
                navigator.serviceWorker.addEventListener('message', a.H);
            }else{
                a.log.warn("Service Worker APIs unavailable.");
            }
            
            for(const b of a.events.values()) b.event.subscribe(b.u);
            a.v=!0;
            a.log.trace("Ready.");
        }())
    }
    
    close(){
        const a=this;return m(function*(){
            a.v||w(`Host ${a.name} is already closed.`);
            a.log.trace("Closing...");
            
            if(typeof navigator!=='undefined' && navigator.serviceWorker){
                navigator.serviceWorker.removeEventListener('message', a.H);
            }
            
            for(const b of a.events.values()){
                b.event.unsubscribe(b.u);
                b.J.splice(0, b.J.length);
            }
            a.v=!1;
            a.log.trace("Closed.")
        }())
    }
    
    onRequest(a,b,c){
        if(b && (b.host==="*"||this.name==="*"||b.host===this.name)&&rbiPackedOurs(b.magic)){
            return Wa(this,a,this.i[b.method],b,d=>{if(c)try{c(d)}catch(e){this.log.warn(`sendResponse failed. ${e.message}.`)}})
        }
    }
};

var Ya=function(){q(this.ea,this);this.F=new Ia;this.F.ba=!1;this.F.sa=!1;this.F.fa=!1;this.wa={}};Ya.prototype.ea=function(a){function b(e){if(e){if(e.value>=ra.value)return"error";if(e.value>=B.value)return"warn";if(e.value>=ta.value)return"log"}return"debug"}if(!this.wa[a.Z]){var c=Ja(this.F,a),d=Za;if(d){const e=b(a.S);$a(d,e,c,a.R)}}};var Za=n.console,$a=function(a,b,c,d){if(a[b])a[b](c,d===void 0?"":d);else a.log(c,d===void 0?"":d)};var I=function(){this.P=this.P;this.T=this.T};I.prototype.P=!1;I.prototype.dispose=function(){this.P||(this.P=!0,this.o())};I.prototype[Symbol.dispose]=function(){this.dispose()};I.prototype.o=function(){if(this.T)for(;this.T.length;)this.T.shift()()};var J=function(a,b){this.type=a;this.currentTarget=this.target=b;this.defaultPrevented=this.I=!1};J.prototype.stopPropagation=function(){this.I=!0};J.prototype.preventDefault=function(){this.defaultPrevented=!0};var ab=function(){if(!n.addEventListener||!Object.defineProperty)return!1;var a=!1,b=Object.defineProperty({},"passive",{get:function(){a=!0}});try{const c=()=>{};n.addEventListener("test",c,b);n.removeEventListener("test",c,b)}catch(c){}return a}();a:for(var bb=["CLOSURE_FLAGS"],cb=n,db=0;db<bb.length;db++)if(cb=cb[bb[db]],cb==null)break a;function K(){var a=n.navigator;return a&&(a=a.userAgent)?a:""}var L;const eb=n.navigator;L=eb?eb.userAgentData||null:null;function M(a){return K().indexOf(a)!=-1};function N(){return!!L&&!!L.platform}function fb(){return M("iPhone")&&!M("iPod")&&!M("iPad")};L&&L.brands.length>0||M("Opera");L&&L.brands.length>0||M("Trident")||M("MSIE");M("Edge");!M("Gecko")||K().toLowerCase().indexOf("webkit")!=-1&&!M("Edge")||M("Trident")||M("MSIE")||M("Edge");var gb=K().toLowerCase().indexOf("webkit")!=-1&&!M("Edge");gb&&M("Mobile");N()||M("Macintosh");N()||M("Windows");(N()?L.platform==="Linux":M("Linux"))||N()||M("CrOS");N()||M("Android");fb();M("iPad");M("iPod");fb()||M("iPad")||M("iPod");K().toLowerCase().indexOf("kaios");function O(a,b){J.call(this,a?a.type:"");this.relatedTarget=this.currentTarget=this.target=null;this.button=this.screenY=this.screenX=this.clientY=this.clientX=this.offsetY=this.offsetX=0;this.key="";this.charCode=this.keyCode=0;this.metaKey=this.shiftKey=this.altKey=this.ctrlKey=!1;this.state=null;this.pointerId=0;this.pointerType="";this.timeStamp=0;this.C=null;a&&this.init(a,b)}r(O,J);
O.prototype.init=function(a,b){const c=this.type=a.type,d=a.changedTouches&&a.changedTouches.length?a.changedTouches[0]:null;this.target=a.target||a.srcElement;this.currentTarget=b;b=a.relatedTarget;b||(c=="mouseover"?b=a.fromElement:c=="mouseout"&&(b=a.toElement));this.relatedTarget=b;d?(this.clientX=d.clientX!==void 0?d.clientX:d.pageX,this.clientY=d.clientY!==void 0?d.clientY:d.pageY,this.screenX=d.screenX||0,this.screenY=d.screenY||0):(this.offsetX=gb||a.offsetX!==void 0?a.offsetX:a.layerX,this.offsetY=
gb||a.offsetY!==void 0?a.offsetY:a.layerY,this.clientX=a.clientX!==void 0?a.clientX:a.pageX,this.clientY=a.clientY!==void 0?a.clientY:a.pageY,this.screenX=a.screenX||0,this.screenY=a.screenY||0);this.button=a.button;this.keyCode=a.keyCode||0;this.key=a.key||"";this.charCode=a.charCode||(c=="keypress"?a.keyCode:0);this.ctrlKey=a.ctrlKey;this.altKey=a.altKey;this.shiftKey=a.shiftKey;this.metaKey=a.metaKey;this.pointerId=a.pointerId||0;this.pointerType=a.pointerType;this.state=a.state;this.timeStamp=
a.timeStamp;this.C=a;a.defaultPrevented&&O.B.preventDefault.call(this)};O.prototype.stopPropagation=function(){O.B.stopPropagation.call(this);this.C.stopPropagation?this.C.stopPropagation():this.C.cancelBubble=!0};O.prototype.preventDefault=function(){O.B.preventDefault.call(this);const a=this.C;a.preventDefault?a.preventDefault():a.returnValue=!1};var hb="closure_listenable_"+(Math.random()*1E6|0),P=function(a){return!(!a||!a[hb])};var ib=0;function jb(a,b,c,d,e){this.listener=a;this.proxy=null;this.src=b;this.type=c;this.capture=!!d;this.u=e;this.key=++ib;this.removed=this.N=!1}var Q=function(a){a.removed=!0;a.listener=null;a.proxy=null;a.src=null;a.u=null};function kb(a,b,c){for(const d in a)b.call(c,a[d],d,a)}function lb(a,b){for(const c in a)if(b.call(void 0,a[c],c,a))return!0;return!1}const mb="constructor hasOwnProperty isPrototypeOf propertyIsEnumerable toLocaleString toString valueOf".split(" ");function nb(a,b){let c,d;for(let e=1;e<arguments.length;e++){d=arguments[e];for(c in d)a[c]=d[c];for(let f=0;f<mb.length;f++)c=mb[f],Object.prototype.hasOwnProperty.call(d,c)&&(a[c]=d[c])}};function R(a){this.src=a;this.g={};this.K=0}R.prototype.add=function(a,b,c,d,e){const f=a.toString();a=this.g[f];a||(a=this.g[f]=[],this.K++);const g=ob(a,b,d,e);g>-1?(b=a[g],c||(b.N=!1)):(b=new jb(b,this.src,f,!!d,e),b.N=c,a.push(b));return b};R.prototype.remove=function(a,b,c,d){a=a.toString();if(!(a in this.g))return!1;const e=this.g[a];b=ob(e,b,c,d);return b>-1?(Q(e[b]),Array.prototype.splice.call(e,b,1),e.length==0&&(delete this.g[a],this.K--),!0):!1};
var pb=function(a,b){const c=b.type;if(c in a.g){var d=a.g[c],e=pa(d,b),f;(f=e>=0)&&Array.prototype.splice.call(d,e,1);f&&(Q(b),a.g[c].length==0&&(delete a.g[c],a.K--))}};R.prototype.removeAll=function(a){a=a&&a.toString();let b=0;for(const c in this.g)if(!a||c==a){const d=this.g[c];for(let e=0;e<d.length;e++)++b,Q(d[e]);delete this.g[c];this.K--}return b};R.prototype.G=function(a,b,c,d){a=this.g[a.toString()];let e=-1;a&&(e=ob(a,b,c,d));return e>-1?a[e]:null};
R.prototype.hasListener=function(a,b){const c=a!==void 0,d=c?a.toString():"",e=b!==void 0;return lb(this.g,function(f){for(let g=0;g<f.length;++g)if(!(c&&f[g].type!=d||e&&f[g].capture!=b))return!0;return!1})};var ob=function(a,b,c,d){for(let e=0;e<a.length;++e){const f=a[e];if(!f.removed&&f.listener==b&&f.capture==!!c&&f.u==d)return e}return-1};var qb="closure_lm_"+(Math.random()*1E6|0),rb={},sb=0,ub=function(a,b,c,d,e){if(d&&d.once)return tb(a,b,c,d,e);if(Array.isArray(b)){for(let f=0;f<b.length;f++)ub(a,b[f],c,d,e);return null}c=S(c);return P(a)?a.listen(b,c,p(d)?!!d.capture:!!d,e):vb(a,b,c,!1,d,e)},vb=function(a,b,c,d,e,f){if(!b)throw Error("Invalid event type");const g=p(e)?!!e.capture:!!e;let l=T(a);l||(a[qb]=l=new R(a));c=l.add(b,c,d,g,f);if(c.proxy)return c;d=wb();c.proxy=d;d.src=a;d.listener=c;if(a.addEventListener)ab||(e=g),e===
void 0&&(e=!1),a.addEventListener(b.toString(),d,e);else if(a.attachEvent)a.attachEvent(xb(b.toString()),d);else if(a.addListener&&a.removeListener)a.addListener(d);else throw Error("addEventListener and attachEvent are unavailable.");sb++;return c},wb=function(){const a=yb,b=function(c){return a.call(b.src,b.listener,c)};return b},tb=function(a,b,c,d,e){if(Array.isArray(b)){for(let f=0;f<b.length;f++)tb(a,b[f],c,d,e);return null}c=S(c);return P(a)?a.ja(b,c,p(d)?!!d.capture:!!d,e):vb(a,b,c,!0,d,e)},
zb=function(a,b,c,d,e){if(Array.isArray(b))for(let f=0;f<b.length;f++)zb(a,b[f],c,d,e);else d=p(d)?!!d.capture:!!d,c=S(c),P(a)?a.da(b,c,d,e):a&&(a=T(a))&&(b=a.G(b,c,d,e))&&U(b)},U=function(a){if(typeof a!=="number"&&a&&!a.removed){var b=a.src;if(P(b))pb(b.j,a);else{var c=a.type,d=a.proxy;b.removeEventListener?b.removeEventListener(c,d,a.capture):b.detachEvent?b.detachEvent(xb(c),d):b.addListener&&b.removeListener&&b.removeListener(d);sb--;(c=T(b))?(pb(c,a),c.K==0&&(c.src=null,b[qb]=null)):Q(a)}}},
xb=function(a){return a in rb?rb[a]:rb[a]="on"+a},yb=function(a,b){if(a.removed)a=!0;else{b=new O(b,this);const c=a.listener,d=a.u||a.src;a.N&&U(a);a=c.call(d,b)}return a},T=function(a){a=a[qb];return a instanceof R?a:null},Ab="__closure_events_fn_"+(Math.random()*1E9>>>0),S=function(a){if(typeof a==="function")return a;a[Ab]||(a[Ab]=function(b){return a.handleEvent(b)});return a[Ab]};function V(a){I.call(this);this.X=a;this.A={}}r(V,I);var Bb=[];V.prototype.listen=function(a,b,c,d){Array.isArray(b)||(b&&(Bb[0]=b.toString()),b=Bb);for(let e=0;e<b.length;e++){const f=ub(a,b[e],c||this.handleEvent,d||!1,this.X||this);if(!f)break;this.A[f.key]=f}return this};V.prototype.ja=function(a,b,c,d){return Cb(this,a,b,c,d)};var Cb=function(a,b,c,d,e,f){if(Array.isArray(c))for(let g=0;g<c.length;g++)Cb(a,b,c[g],d,e,f);else{b=tb(b,c,d||a.handleEvent,e,f||a.X||a);if(!b)return a;a.A[b.key]=b}return a};
V.prototype.da=function(a,b,c,d,e){if(Array.isArray(b))for(let f=0;f<b.length;f++)this.da(a,b[f],c,d,e);else c=c||this.handleEvent,d=p(d)?!!d.capture:!!d,e=e||this.X||this,c=S(c),d=!!d,b=P(a)?a.G(b,c,d,e):a?(a=T(a))?a.G(b,c,d,e):null:null,b&&(U(b),delete this.A[b.key])};V.prototype.removeAll=function(){kb(this.A,function(a,b){this.A.hasOwnProperty(b)&&U(a)},this);this.A={}};V.prototype.o=function(){V.B.o.call(this);this.removeAll()};
V.prototype.handleEvent=function(){throw Error("EventHandler.handleEvent not implemented");};var W=function(){I.call(this);this.j=new R(this);this.va=this;this.aa=null};r(W,I);W.prototype[hb]=!0;h=W.prototype;h.addEventListener=function(a,b,c,d){ub(this,a,b,c,d)};h.removeEventListener=function(a,b,c,d){zb(this,a,b,c,d)};
h.dispatchEvent=function(a){var b=this.aa;if(b){var c=[];for(var d=1;b;b=b.aa)c.push(b),++d}b=this.va;d=a.type||a;if(typeof a==="string")a=new J(a,b);else if(a instanceof J)a.target=a.target||b;else{var e=a;a=new J(d,b);nb(a,e)}e=!0;let f,g;if(c)for(g=c.length-1;!a.I&&g>=0;g--)f=a.currentTarget=c[g],e=X(f,d,!0,a)&&e;a.I||(f=a.currentTarget=b,e=X(f,d,!0,a)&&e,a.I||(e=X(f,d,!1,a)&&e));if(c)for(g=0;!a.I&&g<c.length;g++)f=a.currentTarget=c[g],e=X(f,d,!1,a)&&e;return e};
h.o=function(){W.B.o.call(this);this.j&&this.j.removeAll(void 0);this.aa=null};h.listen=function(a,b,c,d){return this.j.add(String(a),b,!1,c,d)};h.ja=function(a,b,c,d){return this.j.add(String(a),b,!0,c,d)};h.da=function(a,b,c,d){this.j.remove(String(a),b,c,d)};var X=function(a,b,c,d){b=a.j.g[String(b)];if(!b)return!0;b=b.concat();let e=!0;for(let f=0;f<b.length;++f){const g=b[f];if(g&&!g.removed&&g.capture==c){const l=g.listener,Eb=g.u||g.src;g.N&&pb(a.j,g);e=l.call(Eb,d)!==!1&&e}}return e&&!d.defaultPrevented};
W.prototype.G=function(a,b,c,d){return this.j.G(String(a),b,c,d)};W.prototype.hasListener=function(a,b){return this.j.hasListener(a!==void 0?String(a):void 0,b)};function Y(){W.call(this);this.V=new V(this);if(typeof navigator!=='undefined'&&typeof window!=='undefined'){this.V.listen(window,["online","offline"],this.xa)}}r(Y,W);Y.prototype.xa=function(){if(typeof navigator!=='undefined'){this.dispatchEvent(navigator.onLine?"online":"offline")}};Y.prototype.o=function(){Y.B.o.call(this);this.V.dispose();this.V=null};const Db=function(){var a=new Qa,b=rbiRPCMethods();for(const [d,e]of a.qa){const f=d;var c=e;if(b.every(g=>f.has(g)))return c}c={};for(const d of b)Object.defineProperty(c,d,{value:(...e)=>a.invoke(d,...e),writable:!1});a.qa.set(new Set(b),c);return c}();
function Fb(){Ga();const a=new Ya;Ka(a.F);Fa(c=>{const d=c.S;d&&d.value>=B.value&&a.ea(c)});let b=!1;Fa(c=>{if(!b){b=!0;try{var d=Db.recordLog;var e={t:c.ca,l:c.S.value,m:c.getMessage(),n:c.Z,s:c.Aa};d.call(Db,e)}catch(f){}finally{b=!1}}})}const Z=new Na("Offscreen Script");
var Gb=function(a){return m(function*(){var b=new Headers;if(a.headers)for(const [e,f]of Object.entries(a.headers))b.set(e,f);a.request.headers=b;Z.debug(`Sending Fetch request to ${a.url}.`);let c;try{c=yield fetch(a.url,a.request)}catch(e){throw Z.debug(`Fetch request to ${a.url} failed. ${e.message}`),e;}b=yield c.text();Z.debug(`Fetch request to ${a.url} ${c.ok?"succeeded":"failed"}. Response: ${JSON.stringify(JSON.parse(b))}`);const d={};for(const [e,f]of c.headers.entries())d[e]=f;return{ok:c.ok,
status:c.status,statusText:c.statusText,headers:d,text:b}}())};class Hb{constructor(){this.host=new Xa;this.pa=new Y;this.oa=new oa;this.la=new oa}initialize(){const a=this;return m(function*(){Ua(a.host,"network:online",a.oa);Ua(a.host,"network:offline",a.la);Ta(a.host,b=>m(function*(){return Gb(b)}()));try{var r=typeof self!=="undefined"?self:typeof window!=="undefined"?window:null;if(r&&r.RBIComponentRPC&&r.RBIComponentRPC.bindHost)r.RBIComponentRPC.bindHost(a.host)}catch(x){}yield a.host.open();a.pa.listen("online",()=>{Z.debug("Online event received");a.oa.publish()});a.pa.listen("offline",()=>{Z.debug("Offline event received");a.la.publish()})}())}}

Fb();
(new Hb).initialize();
const __RBI_EV_LOG_RELAY__=(()=>{const MAX=1200;const state={port:null,buffer:[],seen:new Set(),lastIndex:0,pollTimer:null,streamed:!1};
function key(e){try{return JSON.stringify(e)}catch(x){return String(e&&e.m?e.m:e)}}
function push(entries){const list=Array.isArray(entries)?entries:[entries];list.forEach(e=>{if(!e)return;const k=key(e);if(state.seen.has(k))return;state.seen.add(k);state.buffer.push(e);if(state.seen.size>MAX*2)state.seen.clear();while(state.buffer.length>MAX)state.buffer.shift()});
broadcast(list)}
function broadcast(entries){const g=typeof window!=="undefined"?window:typeof self!=="undefined"?self:null;if(!g||!g.parent||g.parent===g)return;
try{g.parent.postMessage({channel:"rbi-ev-offscreen",type:"log-record",entries,path:"offscreen_log_relay",at:new Date().toISOString()},location.origin||"*")}catch(e){}}
function nativeRuntime(){const g=typeof window!=="undefined"?window:typeof self!=="undefined"?self:null;if(!g)return null;if(g.__RBI_NATIVE_CHROME__&&g.__RBI_NATIVE_CHROME__.runtime)return g.__RBI_NATIVE_CHROME__.runtime;return null}
function connectLogRecord(){const rt=nativeRuntime();if(!rt||!rt.connect||state.port)return state.streamed;
try{const port=rt.connect({name:"log:record"});state.port=port;state.streamed=!0;port.onMessage.addListener(msg=>{push(Array.isArray(msg)?msg:[msg])});
port.onDisconnect.addListener(()=>{state.port=null;state.streamed=!1;setTimeout(connectLogRecord,1500)})}catch(e){}
return state.streamed}
function pollBuffer(){return Db.getLogBuffer().then(buf=>{if(!Array.isArray(buf))return buf;if(buf.length<state.lastIndex)state.lastIndex=0;
const slice=buf.slice(state.lastIndex);state.lastIndex=buf.length;if(slice.length)push(slice);return buf}).catch(()=>[])}
function start(opts){opts=opts||{};connectLogRecord();return pollBuffer().then(buf=>{if(state.pollTimer)clearInterval(state.pollTimer);
state.pollTimer=setInterval(()=>{pollBuffer().catch(()=>{})},opts.pollMs||8000);return{streamed:state.streamed,buffer_lines:Array.isArray(buf)?buf.length:0,path:"offscreen_log_relay"}})}
function snapshot(){return state.buffer.slice()}
return{start,connectLogRecord,pollBuffer,snapshot,get streamed(){return state.streamed}}})();
const __RBI_EV_OFFSCREEN_AGENT__=(()=>{const methods=rbiRPCMethods();
const api={MAGIC:rbiPackedMagic(),path:"offscreen_script",invoke:(method,args)=>{const fn=Db[method];if(typeof fn!=="function")return Promise.reject(new Error("unknown EV RPC: "+method));
return Promise.resolve(fn.apply(Db,args||[]))},probe:()=>api.invoke("hello",[]).then(hello=>api.invoke("getQuirks",[]).then(quirks=>({ok:!0,hello,quirks,agent:"offscreen_script"})).catch(err=>({ok:!0,hello,quirks_error:String(err),agent:"offscreen_script"}))).catch(err=>({ok:!1,error:String(err&&err.message?err.message:err),agent:"offscreen_script"})),
startLogRelay:(opts)=>__RBI_EV_LOG_RELAY__.start(opts),logSnapshot:()=>__RBI_EV_LOG_RELAY__.snapshot()};
methods.forEach(m=>{api[m]=(...args)=>api.invoke(m,args)});return api})();
const g=typeof self!=="undefined"?self:typeof window!=="undefined"?window:null;
if(g){g.__RBI_EV_OFFSCREEN_AGENT__=__RBI_EV_OFFSCREEN_AGENT__;g.__RBI_EV_LOG_RELAY__=__RBI_EV_LOG_RELAY__;g.__RBI_EV_OFFSCREEN_INJECTED__=!0;
if(g.parent&&g.parent!==g){try{g.parent.postMessage({channel:"rbi-ev-offscreen",type:"ready",agent:"offscreen_script"},location.origin||"*")}catch(e){}}
if(typeof g.addEventListener==="function"){g.addEventListener("message",function(ev){const data=ev.data||{};
if(data.channel!=="rbi-ev-offscreen"||!data.type)return;
if(data.type==="rpc"&&data.id){Oa.__skipParentRelay=!0;Oa.__skipSwRelay=!0;__RBI_EV_OFFSCREEN_AGENT__.invoke(data.method,data.args||[]).then(result=>{Oa.__skipParentRelay=!1;Oa.__skipSwRelay=!1;const reply={channel:"rbi-ev-offscreen",type:"rpc-result",id:data.id,ok:!0,result};
if(ev.source&&ev.source.postMessage)ev.source.postMessage(reply,ev.origin||"*")}).catch(err=>{Oa.__skipParentRelay=!1;Oa.__skipSwRelay=!1;const reply={channel:"rbi-ev-offscreen",type:"rpc-result",id:data.id,ok:!1,error:String(err&&err.message?err.message:err)};
if(ev.source&&ev.source.postMessage)ev.source.postMessage(reply,ev.origin||"*")});return}
if(data.type==="log-start"){__RBI_EV_LOG_RELAY__.start(data.opts||{}).then(out=>{if(ev.source&&ev.source.postMessage)ev.source.postMessage({channel:"rbi-ev-offscreen",type:"log-started",id:data.id,ok:!0,out},ev.origin||"*")}).catch(err=>{if(ev.source&&ev.source.postMessage)ev.source.postMessage({channel:"rbi-ev-offscreen",type:"log-started",id:data.id,ok:!1,error:String(err&&err.message?err.message:err)},ev.origin||"*")});return}
if(data.type==="log-snapshot"&&data.id){if(ev.source&&ev.source.postMessage)ev.source.postMessage({channel:"rbi-ev-offscreen",type:"log-snapshot",id:data.id,entries:__RBI_EV_LOG_RELAY__.snapshot()},ev.origin||"*")}})}
function __rbiStartOffscreenAgent(){return __RBI_EV_OFFSCREEN_AGENT__.invoke("hello",[]).then(()=>__RBI_EV_LOG_RELAY__.start({pollMs:8000})).catch(()=>{})}
g.__RBI_EV_OFFSCREEN_START__=__rbiStartOffscreenAgent;
if(!g.__RBI_EV_OFFSCREEN_HOLD_INIT__)__rbiStartOffscreenAgent()}
})();