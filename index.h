const char *HTML_CONTENT = R"=====(
<!DOCTYPE html>
<html>
  <head>
    <title>battery boy</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
      @import url('https://fonts.googleapis.com/css2?family=Silkscreen:wght@400;700&display=swap');
      html, body {
        padding: 0;
        margin: 0;
        font-family: "Silkscreen", sans-serif;
        background-color: #141414;
        scroll-behavior: smooth;
        scrollbar-color: #C1272D #2D2D2D;
        scrollbar-width: thin;
      }

      h1 {
        color: whitesmoke;
        font-size: 40px;
        text-align: center;
      }

      h2 {
        color: whitesmoke;
        text-align: center;
      }

      p {
        color: whitesmoke;
        text-align: center;
        line-height: 200%%;
      }

      a {
        color: whitesmoke;
        text-align: center;
      }

      #batteryContainer {
        width: 100%%;
        display: flex;
        flex-direction: row;
        justify-content: center;
        align-items: center;
        gap: 32px;
      }

      .batteryWrapper {
        height: 300px;
        width: 200px;
      }

      .batteryBackground {
        display: flex;
        flex-direction: column;
        justify-content: space-evenly;
        align-items: center;
        border-radius: 24px;
        height: 200px;
        width: 100px;
        position: relative;
        left: 50%%;
        transform: translate(-50%%, 25%%);
      }

      .batterySlow {
        border: 4px dotted yellow;
      }
    </style>
  </head>
  <body>
    <h1>Battery Boy, at your service.</h1>
    <div id="batteryContainer">
      <div class="batteryWrapper" id="batteryWrapper1">
        <div class="batteryBackground" id="batteryBackground1">
          <h1 id="batteryCharge1">...</h1>
          <p id="batteryVolt1"></p>
        </div>
      </div>
      <div class="batteryWrapper" id="batteryWrapper2">
        <div class="batteryBackground" id="batteryBackground2">
          <h1 id="batteryCharge2">...</h1>
          <p id="batteryVolt2"></p>
        </div>
      </div>
    </div>
    <div id="batteryContainer">
      <div class="batteryWrapper" id="batteryWrapper3">
        <div class="batteryBackground" id="batteryBackground3">
          <h1 id="batteryCharge3">...</h1>
          <p id="batteryVolt3"></p>
        </div>
      </div>
      <div class="batteryWrapper" id="batteryWrapper4">
        <div class="batteryBackground batterySlow" id="batteryBackground4">
          <h1 id="batteryCharge4">...</h1>
          <p id="batteryVolt4"></p>
        </div>
      </div>
    </div>
  </body>
  <script>
    function lerp(a, b, t) {
      return a + (b - a) * t;
    }

    let gateway = `ws://${window.location.hostname}/ws`;
    let websocket;

    window.addEventListener('load', onLoad);

    function initWebSocket() {
      console.log('Trying to open a WebSocket connection...');
      websocket = new WebSocket(gateway);
      websocket.binaryType = 'arraybuffer';
      websocket.onopen    = onOpen;
      websocket.onclose   = onClose;
      websocket.onmessage = onMessage; // <-- add this line
    }

    function onOpen(event) {
      console.log('Connection opened');
    }

    function onClose(event) {
      console.log('Connection closed');
      setTimeout(initWebSocket, 2000);
    }

    function onMessage(event) {
      //console.log("Received data");
      const view = new Uint8Array(event.data);
      //console.log(view[0]);
      switch (view[0]) {
        case 0:
          for (let i = 1; i < view.length; i += 2) {
            let batteryNum = Math.ceil(i / 2);
            let chargePercentValue = view[i];
            let batteryVoltValue = (view[i+1] / 100).toFixed(2);
            //console.log(i + ": " + chargePercentValue);

            let batteryWrapperElement = document.getElementById("batteryWrapper" + batteryNum);
            let batteryBackgroundElement = document.getElementById("batteryBackground" + batteryNum);
            let batteryChargeElement = document.getElementById("batteryCharge" + batteryNum);
            let batteryVoltElement = document.getElementById("batteryVolt" + batteryNum);

            // Battery not present, empty holder
            if (chargePercentValue == 0) {
              batteryWrapperElement.style.background = "radial-gradient(rgb(48, 48, 48) 0%%, transparent 70%%)"
            
              batteryBackgroundElement.style.background = "linear-gradient(to top, grey 0%% 100%%, grey 100%% 100%%)"
            
              batteryChargeElement.textContent = "-";
              batteryVoltElement.textContent = "";
            }
            // Battery present and fully charged
            else if (chargePercentValue == 100) {
              batteryWrapperElement.style.background = "radial-gradient(rgb(0, 200, 0) 0%%, transparent 70%%)"
            
              batteryBackgroundElement.style.background = "linear-gradient(to top, green 0%% 100%%, red 100%% 100%%)"
            
              batteryChargeElement.textContent = chargePercentValue;
              batteryVoltElement.textContent = batteryVoltValue + "V";
            }
            // Battery present and charging
            else {
              let wrapperRVal = Math.round(lerp(128, 0, chargePercentValue/100));
              let wrapperGVal = 128 - wrapperRVal;
              batteryWrapperElement.style.background = "radial-gradient(rgb(" + wrapperRVal + ", " + wrapperGVal + ", 0) 0%%, transparent 70%%)"
            
              batteryBackgroundElement.style.background = "linear-gradient(to top, green 0%% " + (chargePercentValue - 1) + "%%, red " + (chargePercentValue + 1) + "%% 100%%)"
            
              batteryChargeElement.textContent = chargePercentValue;
              batteryVoltElement.textContent = batteryVoltValue + "V";
            }
          }
          break;
      }
    }

    function onLoad(event) {
      initWebSocket();
    }
  </script>
</html>
)=====";