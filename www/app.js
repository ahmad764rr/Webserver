// Navigation and View Switching
document.querySelectorAll('.nav-item').forEach(item => {
  item.addEventListener('click', (e) => {
    // Remove active class from all
    document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
    document.querySelectorAll('.view-section').forEach(v => {
      v.classList.remove('active');
      v.classList.remove('fade-enter');
    });

    // Add active class to clicked
    const target = e.currentTarget;
    target.classList.add('active');
    
    // Show corresponding view
    const viewId = target.getAttribute('data-view');
    const viewElement = document.getElementById(viewId);
    viewElement.classList.add('active');
    
    // Force reflow to trigger animation
    void viewElement.offsetWidth;
    viewElement.classList.add('fade-enter');

    // Update URL bar method badge based on the active tab
    const methodSpan = target.querySelector('.nav-method');
    const urlBarMethod = document.getElementById('current-method');
    urlBarMethod.textContent = methodSpan.textContent;
    urlBarMethod.className = `url-bar-method method-${methodSpan.textContent.toLowerCase()}`;
  });
});

// Tab Switching for Response Panel
function switchResponseTab(tabName) {
  document.getElementById('tab-raw').classList.toggle('active', tabName === 'raw');
  document.getElementById('tab-preview').classList.toggle('active', tabName === 'preview');
  
  document.getElementById('term-container').style.display = tabName === 'raw' ? 'block' : 'none';
  document.getElementById('preview-container').style.display = tabName === 'preview' ? 'block' : 'none';
}

// UI Helpers
function showSpinner(id) {
  const el = document.getElementById(id);
  if(el) el.style.display = 'inline-block';
}

function hideSpinner(id) {
  const el = document.getElementById(id);
  if(el) el.style.display = 'none';
}

// Utility to display response in terminal
async function displayResponse(res, requestUrl) {
  const term = document.getElementById('term-container');
  const preview = document.getElementById('preview-container');
  
  // Basic status color mapping
  let statusClass = 'status-200';
  if (res.status >= 300 && res.status < 400) statusClass = 'status-301';
  if (res.status >= 400 && res.status < 500) statusClass = 'status-404';
  if (res.status >= 500) statusClass = 'status-500';

  // Build header string
  let headersStr = '';
  res.headers.forEach((val, key) => {
    headersStr += `${key}: ${val}\n`;
  });

  // Get body
  const bodyText = await res.text();
  
  // Render Terminal Raw Output
  term.innerHTML = `
<div class="term-status ${statusClass}">HTTP/1.1 ${res.status} ${res.statusText}
Request URL: ${requestUrl}</div>
<div class="term-headers">${headersStr}</div>
<div class="term-body">${escapeHtml(bodyText) || '<i>(empty body)</i>'}</div>
  `;

  // Render Preview HTML
  const contentType = res.headers.get('content-type') || '';
  if (contentType.includes('text/html')) {
    // Inject HTML safely using an iframe to isolate styles
    preview.innerHTML = `<iframe style="width: 100%; height: 100%; border: none;" srcdoc="${escapeHtmlAttr(bodyText)}"></iframe>`;
  } else if (contentType.includes('image/')) {
    preview.innerHTML = `<img style="max-width:100%; border-radius:8px;" src="${requestUrl}" alt="Preview Image" />`;
  } else {
    preview.innerHTML = `<pre style="font-family: Consolas, monospace; white-space: pre-wrap;">${escapeHtml(bodyText)}</pre>`;
  }
}

function escapeHtml(unsafe) {
  return unsafe
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function escapeHtmlAttr(unsafe) {
  return unsafe.replace(/"/g, '&quot;');
}

function updateUrlBar(url) {
  document.getElementById('current-url').value = url;
}


// --- Request Implementations ---

// 1. GET
async function executeGet() {
  const path = document.getElementById('get-path').value || '/';
  updateUrlBar(path);
  showSpinner('spinner-get');
  try {
    const res = await fetch(path, { method: 'GET' });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">Network Error: ${err.message}</div>`;
  } finally {
    hideSpinner('spinner-get');
  }
}

// Autoindex Test
async function executeAutoIndex() {
  const path = 'http://eval3.com:8081/'; // Points to the autoindex server from eval.conf
  updateUrlBar(path);
  showSpinner('spinner-get');
  try {
    const res = await fetch(path, { method: 'GET' });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">Network Error or CORS block: ${err.message}. Ensure you resolve eval3.com to localhost in your hosts file or run tests locally.</div>`;
  } finally {
    hideSpinner('spinner-get');
  }
}


// 2. File Upload Dropzone logic
let selectedFile = null;
const fileInput = document.getElementById('file-input');
const dropZone = document.getElementById('drop-zone');
const selectedFileName = document.getElementById('selected-file-name');

fileInput.addEventListener('change', (e) => {
  if (e.target.files.length > 0) {
    selectedFile = e.target.files[0];
    selectedFileName.textContent = `Selected: ${selectedFile.name} (${(selectedFile.size / 1024).toFixed(2)} KB)`;
    const uploadPathInput = document.getElementById('upload-path');
    if (uploadPathInput) {
      uploadPathInput.value = `/upload/${selectedFile.name}`;
    }
  }
});

dropZone.addEventListener('dragover', (e) => {
  e.preventDefault();
  dropZone.classList.add('dragover');
});

dropZone.addEventListener('dragleave', () => {
  dropZone.classList.remove('dragover');
});

dropZone.addEventListener('drop', (e) => {
  e.preventDefault();
  dropZone.classList.remove('dragover');
  if (e.dataTransfer.files.length > 0) {
    selectedFile = e.dataTransfer.files[0];
    selectedFileName.textContent = `Selected: ${selectedFile.name} (${(selectedFile.size / 1024).toFixed(2)} KB)`;
    const uploadPathInput = document.getElementById('upload-path');
    if (uploadPathInput) {
      uploadPathInput.value = `/upload/${selectedFile.name}`;
    }
  }
});

async function executeUpload() {
  const path = document.getElementById('upload-path').value;
  if (!selectedFile) {
    alert('Please select a file to upload first.');
    return;
  }
  updateUrlBar(path);
  showSpinner('spinner-upload');
  
  try {
    // Read file as ArrayBuffer to send raw binary (simulating curl --data-binary)
    // We could use FormData but standard curl --data-binary sends the raw content
    const arrayBuffer = await selectedFile.arrayBuffer();
    
    const res = await fetch(path, {
      method: 'POST',
      body: arrayBuffer,
      headers: {
        'Content-Type': selectedFile.type || 'application/octet-stream'
      }
    });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">Upload Error: ${err.message}</div>`;
  } finally {
    hideSpinner('spinner-upload');
  }
}

// 3. DELETE
async function executeDelete() {
  const path = document.getElementById('delete-path').value;
  updateUrlBar(path);
  showSpinner('spinner-delete');
  try {
    const res = await fetch(path, { method: 'DELETE' });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">Delete Error: ${err.message}</div>`;
  } finally {
    hideSpinner('spinner-delete');
  }
}

// 4. CGI GET
async function executeCgiGet() {
  const query = document.getElementById('cgi-query').value;
  const path = `/cgi-bin/test_script.py${query ? '?' + query : ''}`;
  updateUrlBar(path);
  showSpinner('spinner-cgi-get');
  try {
    const res = await fetch(path, { method: 'GET' });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">CGI Error: ${err.message}</div>`;
  } finally {
    hideSpinner('spinner-cgi-get');
  }
}

// 5. CGI POST
async function executeCgiPost() {
  const path = `/cgi-bin/test_script.py`;
  const bodyData = document.getElementById('cgi-body').value;
  updateUrlBar(path);
  showSpinner('spinner-cgi-post');
  try {
    const res = await fetch(path, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: bodyData
    });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">CGI Error: ${err.message}</div>`;
  } finally {
    hideSpinner('spinner-cgi-post');
  }
}

// 6. Errors (Generic Tester)
async function testError(path, method) {
  updateUrlBar(path);
  document.getElementById('current-method').textContent = method;
  try {
    const res = await fetch(path, { method: method });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">Error: ${err.message}</div>`;
  }
}

async function testPayloadTooLarge() {
  const path = 'http://eval2.com:8080/'; // eval2.com has max_body_size 10
  updateUrlBar(path);
  document.getElementById('current-method').textContent = 'POST';
  try {
    const res = await fetch(path, {
      method: 'POST',
      body: "This is a payload much larger than 10 bytes to trigger a 413 error."
    });
    await displayResponse(res, path);
  } catch (err) {
    document.getElementById('term-container').innerHTML = `<div class="status-500">Network Error or CORS block: ${err.message}. Ensure eval2.com resolves to localhost.</div>`;
  }
}
