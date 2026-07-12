import os, time, json
os.environ["DISPLAY"]=":99"
os.environ["VK_ICD_FILENAMES"]="/tmp/nvidia_icd_fixed.json"
os.environ["ANGLE_FORCE_WEBGPU"]="1"
os.environ["DAWN_DEBUG_BREAK_ON_ERROR"]="0"

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By
from selenium.webdriver.common.action_chains import ActionChains

opts = Options()
opts.binary_location = "/opt/google/chrome/chrome"
for a in ["--no-sandbox","--disable-gpu-sandbox",
          "--use-gl=angle","--enable-webgl","--enable-gpu",
          "--ignore-gpu-blocklist","--disable-gpu-rasterization",
          "--disable-dev-shm-usage","--window-size=1280,720",
          "--disable-background-timer-throttling",
          "--disable-renderer-backgrounding",
          "--disable-backgrounding-occluded-windows"]:
    opts.add_argument(a)
opts.set_capability('goog:loggingPrefs', {'browser': 'ALL'})

service = Service("/root/motionmark_bench/chromedriver-linux64/chromedriver")
driver = webdriver.Chrome(service=service, options=opts)
driver.set_page_load_timeout(120)

# Verify WebGPU backend
driver.get("data:text/html,<canvas></canvas><script>var gl=document.createElement('canvas').getContext('webgl');var ext=gl.getExtension('WEBGL_debug_renderer_info');document.title=ext?gl.getParameter(ext.UNMASKED_RENDERER_WEBGL):'no-ext';</script>")
time.sleep(2)
print(f"GL_RENDERER: {driver.title}", flush=True)

# Load MotionMark
print("Loading MotionMark 1.3.1...", flush=True)
driver.get("https://browserbench.org/MotionMark1.3.1/developer.html")
time.sleep(8)
print(f"Page: {driver.title}", flush=True)

# Select Multiply test using proper event dispatching
print("Selecting Multiply test...", flush=True)
driver.execute_script("""
// Uncheck all with proper events
var cbs = document.querySelectorAll('#suites input[type=checkbox]');
for(var i=0; i<cbs.length; i++){
    if(cbs[i].checked){
        cbs[i].checked = false;
        cbs[i].dispatchEvent(new Event('change', {bubbles: true}));
    }
}
""")
time.sleep(0.5)

# Select Multiply
driver.execute_script("""
var items = document.querySelectorAll('#suites li');
for(var i=0; i<items.length; i++){
    var text = items[i].textContent;
    if(text.indexOf('Multiply') >= 0 && text.indexOf('MotionMark') < 0 && text.indexOf('Canvas') < 0){
        var cb = items[i].querySelector('input[type=checkbox]');
        if(cb){
            cb.checked = true;
            cb.dispatchEvent(new Event('change', {bubbles: true}));
            break;
        }
    }
}
""")
time.sleep(1)

# Verify button is enabled
btn_state = driver.execute_script("""
var btn = document.getElementById('run-benchmark');
return {disabled: btn.disabled, text: btn.textContent.trim()};
""")
print(f"Button: {json.dumps(btn_state)}", flush=True)

if btn_state['disabled']:
    print("ERROR: Button still disabled!", flush=True)
    driver.quit()
    exit(1)

# Click the button!
print("Starting benchmark...", flush=True)
driver.execute_script("document.getElementById('run-benchmark').click();")
time.sleep(3)

# Check state
state = driver.execute_script("""
var sec = document.querySelector('section.selected');
return {section: sec ? sec.id : 'none', hasIframe: !!document.querySelector('iframe')};
""")
print(f"State after click: {json.dumps(state)}", flush=True)

if state['section'] != 'test-container':
    # Try startBenchmark directly
    print("Button click didn't work, trying startBenchmark()...", flush=True)
    driver.execute_script("benchmarkController.startBenchmark();")
    time.sleep(3)
    state = driver.execute_script("""
    var sec = document.querySelector('section.selected');
    return {section: sec ? sec.id : 'none', hasIframe: !!document.querySelector('iframe')};
    """)
    print(f"State after startBenchmark(): {json.dumps(state)}", flush=True)

if state['section'] != 'test-container' and not state.get('hasIframe'):
    print("ERROR: Benchmark did not start!", flush=True)
    # Debug more
    debug = driver.execute_script("""
    var out = {};
    try { out.suitesMgr = typeof suitesManager; } catch(e) {}
    try { out.selected = suitesManager.isAtLeastOneTestSelected(); } catch(e) { out.selErr = e.message; }
    try { out.suitesCount = suitesManager._suites.length; } catch(e) {}
    try {
        out.selectedTests = [];
        var suites = suitesManager._suites;
        for(var i=0;i<suites.length;i++){
            var tests = suites[i].tests;
            for(var j=0;j<tests.length;j++){
                if(tests[j].disabled !== true) out.selectedTests.push(tests[j].name || suites[i].name);
            }
        }
    } catch(e) { out.testErr = e.message; }
    return out;
    """)
    print(f"Debug: {json.dumps(debug, indent=2)}", flush=True)
    driver.quit()
    exit(1)

# Wait for completion (up to 5 min for single test)
print("Benchmark running, waiting for results...", flush=True)
for i in range(100):
    try:
        result = driver.execute_script("var r=document.getElementById('results');return(r&&r.className.indexOf('selected')>=0)?'done':'running'")
        if result == "done":
            print(f"  Completed in {i*3}s!", flush=True)
            break
    except:
        pass
    if i % 10 == 0:
        print(f"  {i*3}s...", flush=True)
    time.sleep(3)
else:
    print("  TIMEOUT 300s", flush=True)

time.sleep(2)

# Extract results
data = driver.execute_script("""
var out = {};
try {
    var scoreEl = document.querySelector('.score');
    out.score = scoreEl ? scoreEl.textContent.trim() : 'none';
} catch(e) {}
try {
    if (typeof benchmarkController !== 'undefined' && benchmarkController._results) {
        var r = benchmarkController._results;
        out.results = {};
        for (var k in r) {
            if (r[k]) out.results[k] = {score: r[k].score, complexity: r[k].complexity};
        }
    }
} catch(e) { out.err = e.message; }
try {
    var table = document.getElementById('results-score');
    out.table = table ? table.innerText : '';
} catch(e) {}
return out;
""")

print("\n" + "="*60, flush=True)
print(" MotionMark 1.3.1 - ANGLE WebGPU Backend (Multiply)", flush=True)
print("="*60, flush=True)
print(json.dumps(data, indent=2), flush=True)

# Save
with open("/root/motionmark_bench/webgpu_multiply_result.json", "w") as f:
    json.dump(data, f, indent=2)
print("\nSaved to webgpu_multiply_result.json", flush=True)

driver.save_screenshot("/root/motionmark_bench/webgpu_multiply_screenshot.png")
driver.quit()
