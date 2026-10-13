// DualSense <-> Beitong Translation Bridge Web Controller
document.addEventListener('DOMContentLoaded', () => {
  // UI Elements
  const gainForceSlider = document.getElementById('gain-force');
  const valGainForce = document.getElementById('val-gain-force');
  const gainVibeSlider = document.getElementById('gain-vibe');
  const valGainVibe = document.getElementById('val-gain-vibe');
  const gainRumbleSlider = document.getElementById('gain-rumble');
  const valGainRumble = document.getElementById('val-gain-rumble');
  const minFloorSlider = document.getElementById('min-floor');
  const valMinFloor = document.getElementById('val-min-floor');

  // Trigger telemetry visualizer elements
  const ltFill = document.getElementById('lt-fill');
  const ltMarker = document.getElementById('lt-marker');
  const ltDepth = document.getElementById('lt-depth');
  const ltForce = document.getElementById('lt-force');
  const ltModeBadge = document.getElementById('lt-mode-badge');

  const rtFill = document.getElementById('rt-fill');
  const rtMarker = document.getElementById('rt-marker');
  const rtDepth = document.getElementById('rt-depth');
  const rtForce = document.getElementById('rt-force');
  const rtModeBadge = document.getElementById('rt-mode-badge');

  const rumbleLeft = document.getElementById('rumble-left');
  const rumbleLeftVal = document.getElementById('rumble-left-val');
  const rumbleRight = document.getElementById('rumble-right');
  const rumbleRightVal = document.getElementById('rumble-right-val');

  // Sliders binding
  gainForceSlider.addEventListener('input', (e) => {
    valGainForce.textContent = parseFloat(e.target.value).toFixed(2) + 'x';
  });

  gainVibeSlider.addEventListener('input', (e) => {
    valGainVibe.textContent = parseFloat(e.target.value).toFixed(2) + 'x';
  });

  gainRumbleSlider.addEventListener('input', (e) => {
    valGainRumble.textContent = parseFloat(e.target.value).toFixed(2) + 'x';
  });

  minFloorSlider.addEventListener('input', (e) => {
    valMinFloor.textContent = e.target.value + '%';
  });

  // Curve Segmented Buttons
  const segButtons = document.querySelectorAll('.seg-btn');
  segButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      segButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
    });
  });

  // Reset Defaults
  document.getElementById('btn-reset-defaults').addEventListener('click', () => {
    gainForceSlider.value = 1.45;
    valGainForce.textContent = '1.45x';

    gainVibeSlider.value = 1.50;
    valGainVibe.textContent = '1.50x';

    gainRumbleSlider.value = 1.35;
    valGainRumble.textContent = '1.35x';

    minFloorSlider.value = 25;
    valMinFloor.textContent = '25%';

    segButtons.forEach(b => b.classList.remove('active'));
    segButtons[1].classList.add('active'); // Intense
  });

  // Studio Test Action Handlers
  document.getElementById('btn-test-pistol').addEventListener('click', () => {
    rtModeBadge.textContent = 'Pistol Detent Wall (0x25)';
    rtModeBadge.className = 'trigger-mode-badge badge-weapon';
    rtFill.style.height = '35%';
    rtDepth.textContent = '35%';
    rtMarker.style.bottom = '30%';
    rtForce.textContent = '85% (Sharp Snap)';
    
    rumbleRight.style.width = '70%';
    rumbleRightVal.textContent = '178 / 255';
  });

  document.getElementById('btn-test-shotgun').addEventListener('click', () => {
    rtModeBadge.textContent = 'Heavy Shotgun Detent (0x25)';
    rtModeBadge.className = 'trigger-mode-badge badge-weapon';
    rtFill.style.height = '65%';
    rtDepth.textContent = '65%';
    rtMarker.style.bottom = '60%';
    rtForce.textContent = '100% (Solid Wall)';

    rumbleLeft.style.width = '95%';
    rumbleLeftVal.textContent = '242 / 255';
    rumbleRight.style.width = '85%';
    rumbleRightVal.textContent = '216 / 255';
  });

  document.getElementById('btn-test-rifle').addEventListener('click', () => {
    rtModeBadge.textContent = 'Recoil Pulse Vibration (0x26)';
    rtModeBadge.className = 'trigger-mode-badge';
    rtFill.style.height = '85%';
    rtDepth.textContent = '85%';
    rtMarker.style.bottom = '15%';
    rtForce.textContent = '95% (180 Hz Recoil)';

    // Pulse animation
    let count = 0;
    const interval = setInterval(() => {
      rumbleRight.style.width = count % 2 === 0 ? '90%' : '30%';
      rumbleRightVal.textContent = count % 2 === 0 ? '230 / 255' : '76 / 255';
      count++;
      if (count > 6) clearInterval(interval);
    }, 100);
  });

  document.getElementById('btn-test-reset').addEventListener('click', () => {
    rtModeBadge.textContent = 'Linear / Off';
    rtModeBadge.className = 'trigger-mode-badge';
    rtFill.style.height = '0%';
    rtDepth.textContent = '0%';
    rtMarker.style.bottom = '0%';
    rtForce.textContent = '0%';

    ltModeBadge.textContent = 'Linear / Off';
    ltFill.style.height = '0%';
    ltDepth.textContent = '0%';
    ltMarker.style.bottom = '0%';
    ltForce.textContent = '0%';

    rumbleLeft.style.width = '0%';
    rumbleLeftVal.textContent = '0 / 255';
    rumbleRight.style.width = '0%';
    rumbleRightVal.textContent = '0 / 255';
  });
});
