import os, time, json
os.environ["DISPLAY"]=":99"
os.environ["VK_ICD_FILENAMES"]="/tmp/nvidia_icd_fixed.json"
os.environ["ANGLE_FORCE_WEBGPU"]="1"
os.environ["DAWN_DEBUG_BREAK_ON_ERROR"]="0"

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.chrome.service import Service
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

# Verify WebGPU
driver.get("data:text/html,<canvas></canvas><script>var gl=document.createElement('canvas').getContext('webgl');var ext=gl.getExtension('WEBGL_debug_renderer_info');document.title=ext?gl.getParameter(ext.UNMASKED_RENDERER_WEBGL):'no-ext';</script>")
time.sleep(2)
print(f"GL_RENDERER: {driver.title}", flush=True)

# Load MotionMark
print("Loading MotionMark 1.2...", flush=True)
driver.get("https://browserbench.org/MotionMark1.2/developer.html")
time.sleep(8)
print(f"Page: {driver.title}", flush=True)

# Select ALL MotionMark suite tests using proper event dispatching
print("Selecting MotionMark suite (all tests)...", flush=True)

# First uncheck all
driver.execute_script("""
var cbs = document.querySelectorAll('#suites input[type=checkbox]');
for(var i=0; i<cbs.length; i++){
    if(cbs[i].checked){
        cbs[i].checked = false;
        cbs[i].dispatchEvent(new Event('change', {bubbles: true}));
    }
}
""")
time.sleep(0.5)

# Select the top-level MotionMark suite checkbox (selects all 8 subtests)
driver.execute_script("""
var topItems = document.querySelectorAll('#suites > ul.tree > li');
if(topItems.length > 0){
    var cb = topItems[0].querySelector('label > input[type=checkbox]');
    if(cb){
        cb.checked = true;
        cb.dispatchEvent(new Event('change', {bubbles: true}));
    }
    // Also check all children
    var children = topItems[0].querySelectorAll('ul input[type=checkbox]');
    for(var i=0; i<children.length; i++){
        children[i].checked = true;
        children[i].dispatchEvent(new Event('change', {bubbles: true}));
    }
}
""")
time.sleep(1)

# Verify
info = driver.execute_script("""
var cbs = document.querySelectorAll('#suites input[type=checkbox]');
var checked = 0;
for(var i=0;i<cbs.length;i++) if(cbs[i].checked) checked++;
var btn = document.getElementById('run-benchmark');
return {checked: checked, btnDisabled: btn.disabled, selected: suitesManager.isAtLeastOneTestSelected()};
""")
print(f"Selection: {json.dumps(info)}", flush=True)

if info['btnDisabled']:
    print("ERROR: Button disabled!", flush=True)
    driver.quit()
    exit(1)

# Start!
print("Starting full MotionMark suite...", flush=True)
driver.execute_script("document.getElementById('run-benchmark').click();")
time.sleep(3)

state = driver.execute_script("""
var sec = document.querySelector('section.selected');
return {section: sec ? sec.id : 'none', hasIframe: !!document.querySelector('iframe')};
""")
print(f"State: {json.dumps(state)}", flush=True)

if state['section'] != 'test-container':
    print("ERROR: Did not start!", flush=True)
    driver.quit()
    exit(1)

# Wait for results (up to 10 min for all 8 tests)
print("Running all tests (expect ~4-5 minutes)...", flush=True)
for i in range(200):
    try:
        result = driver.execute_script("var r=document.getElementById('results');return(r&&r.className.indexOf('selected')>=0)?'done':'running'")
        if result == "done":
            print(f"  Completed in {i*3}s!", flush=True)
            break
    except:
        pass
    if i % 20 == 0:
        print(f"  {i*3}s...", flush=True)
    time.sleep(3)
else:
    print("  TIMEOUT 600s", flush=True)

time.sleep(3)

# Press 'j' for JSON
try:
    ActionChains(driver).send_keys('j').perform()
    time.sleep(2)
except:
    pass

# Extract results
data = driver.execute_script("""
var out = {};
try {
    var scoreEl = document.querySelector('.score');
    out.overallScore = scoreEl ? scoreEl.textContent.trim() : 'none';
} catch(e) {}
try {
    if (typeof benchmarkController !== 'undefined' && benchmarkController._results) {
        var r = benchmarkController._results;
        out.tests = {};
        for (var k in r) {
            if (r[k]) out.tests[k] = {score: r[k].score, complexity: r[k].complexity};
        }
    }
} catch(e) { out.err = e.message; }
try {
    var table = document.getElementById('results-score');
    out.table = table ? table.innerText : '';
} catch(e) {}
try {
    var pre = document.querySelector('pre');
    out.json = pre ? pre.textContent.substring(0, 15000) : '';
} catch(e) {}
return out;
""")

print("\n" + "="*60, flush=True)
print(" MotionMark 1.2 - ANGLE WebGPU Backend (Full Suite)", flush=True)
print("="*60, flush=True)

if data.get('overallScore'):
    print(f"\n  OVERALL SCORE: {data['overallScore']}", flush=True)

if data.get('tests'):
    print("\n  Per-test scores:", flush=True)
    for name, vals in data['tests'].items():
        print(f"    {name}: {vals.get('score', 'N/A')}", flush=True)

if data.get('table'):
    print(f"\n  Results table:\n{data['table'][:2000]}", flush=True)

# Save
with open("/root/motionmark_bench/webgpu_full_results.json", "w") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
print("\nSaved to webgpu_full_results.json", flush=True)

driver.save_screenshot("/root/motionmark_bench/webgpu_full_screenshot.png")
driver.quit()
