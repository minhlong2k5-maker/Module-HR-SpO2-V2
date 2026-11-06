#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Health Monitor Dashboard</title>
  <style>
    :root { --bg: #f4f7f6; --card: #ffffff; --text: #2c3e50; --blue: #3498db; --green: #2ecc71; --red: #e74c3c; --border: #e0e6ed; --shadow: rgba(0, 0, 0, 0.05); }
    body { background-color: var(--bg); color: var(--text); font-family: 'Segoe UI', system-ui, sans-serif; margin: 0; padding: 20px; }
    .container { max-width: 1000px; margin: auto; }
    .header { text-align: center; margin-bottom: 25px; }
    .header h1 { margin: 0; color: #2c3e50; font-weight: 900; }
    .grid-top { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 20px; }
    .card { background: var(--card); padding: 25px; border-radius: 12px; box-shadow: 0 4px 15px var(--shadow); border: 1px solid var(--border); }
    .val-box { text-align: center; }
    .val-title { font-size: 1rem; color: #7f8c8d; font-weight: 700; margin-bottom: 15px; }
    .val { font-size: 3.5rem; font-weight: 900; color: var(--blue); line-height: 1; }
    .val.spo2 { color: var(--green); }
    .chart-box { height: 350px; margin-bottom: 20px; position: relative; width: 100%; box-sizing: border-box;}
    canvas { width: 100%; height: 100%; border-radius: 8px; background: #fff;}
    .btn { background: var(--blue); color: white; border: none; padding: 12px 20px; border-radius: 8px; font-weight: bold; cursor: pointer; width: 100%; transition: 0.2s;}
    .btn-danger { background: var(--red); width: auto; padding: 6px 12px; border-radius: 6px;}
    .btn-danger-full { background: var(--red); margin-top: 15px; }
    input[type=file] { display: block; width: 100%; margin-bottom: 15px; padding: 10px; border: 1px dashed #bdc3c7; border-radius: 8px; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 12px; border-bottom: 1px solid var(--border); }
    #ws-status { position: absolute; top: 20px; right: 20px; font-size: 0.85rem; font-weight: bold; padding: 6px 15px; border-radius: 20px; }
  </style>
</head>
<body>
  <div id="ws-status">Đang kết nối...</div>
  <div class="container">
    <div class="header"><h1>MEDICAL DASHBOARD</h1></div>
    <div class="grid-top">
      <div class="card val-box"><div class="val-title">NHỊP TIM (BPM)</div><div class="val" id="bpmVal">--</div></div>
      <div class="card val-box"><div class="val-title">SPO2 (%)</div><div class="val spo2" id="spo2Val">--</div></div>
    </div>
    <div class="card chart-box">
      <div class="val-title" style="text-align: left;">ĐỒ THỊ TÍN HIỆU (PPG)</div>
      <canvas id="ppgChart"></canvas>
    </div>
    <div class="card">
      <div class="val-title" style="text-align: left; border-bottom: 1px solid #eee; padding-bottom:10px;">QUẢN LÝ THẺ NHỚ SD</div>
      <input type="file" id="fileInput" multiple>
      <button class="btn" onclick="uploadFiles()">TẢI LÊN THẺ NHỚ</button>
      <div id="status" style="text-align:center; margin-top:15px; color:#3498db; font-weight:bold;"></div>
      <div id="fileList" style="margin-top:20px; max-height: 250px; overflow-y: auto;">Đang tải...</div>
      <button class="btn btn-danger-full" onclick="deleteAll()">XÓA SẠCH THẺ NHỚ</button>
    </div>
  </div>

  <script>
    const canvas = document.getElementById('ppgChart');
    const ctx = canvas.getContext('2d');
    const dataPoints = 200;
    let ppgData = new Array(dataPoints).fill(0);
    
    function resizeCanvas() {
      canvas.width = canvas.parentElement.clientWidth - 50;
      canvas.height = canvas.parentElement.clientHeight - 60;
    }
    window.addEventListener('resize', resizeCanvas);
    
    function drawChart() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      let min = Math.min(...ppgData);
      let max = Math.max(...ppgData);
      let range = (max - min) || 1;
      
      ctx.beginPath();
      ctx.strokeStyle = '#3498db';
      ctx.lineWidth = 2;
      ctx.lineJoin = 'round';
      
      for(let i = 0; i < dataPoints; i++) {
        let x = (i / (dataPoints - 1)) * canvas.width;
        let y = canvas.height - (((ppgData[i] - min) / range) * canvas.height * 0.8 + canvas.height * 0.1); 
        if(i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
      requestAnimationFrame(drawChart);
    }

    let ws;
    function initWS() {
      ws = new WebSocket('ws://' + window.location.hostname + '/ws');
      ws.onopen = () => { 
        let st = document.getElementById('ws-status');
        st.innerText = '🟢 Đã kết nối'; st.style.color = '#27ae60'; st.style.background = '#d5f5e3';
      };
      ws.onclose = () => { 
        let st = document.getElementById('ws-status');
        st.innerText = '🔴 Mất kết nối'; st.style.color = '#c0392b'; st.style.background = '#fadbd8';
        setTimeout(initWS, 2000); 
      };
      ws.onmessage = (e) => {
        try {
          const d = JSON.parse(e.data);
          if(d.bpm > 0) document.getElementById('bpmVal').innerText = d.bpm;
          if(d.spo2 > 0) document.getElementById('spo2Val').innerText = d.spo2;
          
          ppgData.push(d.ir);
          ppgData.shift();
        } catch (err) {}
      };
    }

    window.addEventListener('DOMContentLoaded', () => { 
      resizeCanvas(); drawChart(); initWS(); loadFiles(); 
    });
    
    function loadFiles() { fetch('/list').then(r => r.text()).then(h => document.getElementById('fileList').innerHTML = h); }
    async function uploadFiles() {
      const files = document.getElementById('fileInput').files;
      if(!files.length) return;
      for(let i=0; i<files.length; i++) {
        let fd = new FormData(); fd.append("file", files[i]);
        document.getElementById('status').innerText = `Đang tải: ${files[i].name}...`;
        await fetch('/upload', { method: "POST", body: fd });
      }
      document.getElementById('status').innerText = "Tải lên hoàn tất!"; document.getElementById('fileInput').value = ""; loadFiles();
    }
    function deleteFile(f) { if(confirm(`Xóa ${f}?`)) fetch(`/delete?file=${encodeURIComponent(f)}`).then(loadFiles); }
    function deleteAll() { if(confirm('Xóa sạch thẻ?')) fetch('/reset').then(loadFiles); }
  </script>
</body>
</html>
)rawliteral";

#endif