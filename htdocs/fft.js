'use strict';


function get_appropriate_ws_url(extra_url)
{
	var pcol;
	var u = document.URL;

	/*
	 * We open the websocket encrypted if this page came on an
	 * https:// url itself, otherwise unencrypted
	 */

	if (u.substring(0, 5) === "https") {
		pcol = "wss://";
		u = u.substr(8);
	} else {
		pcol = "ws://";
		if (u.substring(0, 4) === "http")
			u = u.substr(7);
	}

	u = u.split("/");

	/* + "/xxx" bit is for IE10 workaround */

	return pcol + u[0] + "/" + extra_url;
}

function new_ws(urlpath, protocol)
{
	return new WebSocket(urlpath, protocol);
}

let colourMap;
let wf;
let sp;

let render_timer;
const render_interval = 100; // ms
let render_busy = false;
let render_buffer = [];

window.addEventListener('load', () =>
{

	let ws = new_ws(get_appropriate_ws_url(""), "fft-main");
    ws.binaryType = 'arraybuffer';
	try {
		ws.onopen = function() {
			console.log("ws opened");
		};
	
		ws.onmessage =function got_packet(msg) {
			try
			  {
			    const parsed_fft = new Uint16Array(msg.data);
			    if(parsed_fft != null)
			    {
			      render_buffer.push(parsed_fft);
			    }
			  }
			  catch(e)
			  {
			    console.log("Error parsing binary!",e);
			  }
		};
	
		ws.onclose = function(){
			console.log("ws closed");
		};
	} catch(exception) {
		alert("<p>Error " + exception);  
	}

  colourMap = new ColourMap();

  wf = new Waterfall("waterfall", colourMap);

  sp = new Spectrum("spectrum", colourMap);
	
}, false);

render_timer = setInterval(function()
{
  if(!render_busy)
  {
    render_busy = true;
    if(render_buffer.length > 0)
    {
      /* Pull oldest frame off the buffer and render it */
      var data_frame = render_buffer.shift();

      wf.addLine(data_frame);
      sp.updateData(data_frame);

      /* If we're buffering up, remove old queued frames (unsure about this) */
      if(render_buffer.length > 2)
      {
        render_buffer.splice(0, render_buffer.length - 2);
      }
    }
    else
    {
    	console.log("Premature render, no data available.");
    }
    render_busy = false;
  }
  else
  {
    console.log("Slow render blocking next frame, configured interval is ", render_interval);
  }
}, render_interval);


function Spectrum(spectrumCanvasId, colourMap)
{
    this.width = document.getElementById(spectrumCanvasId).clientWidth;
    this.height = document.getElementById(spectrumCanvasId).clientHeight;

    this.ctx = document.getElementById(spectrumCanvasId).getContext("2d");

    this.map = colourMap;

    this.updateData = function(data)
    {
      var dataLength = data.length;
      var i;
      var sample_index;
      var sample_index_f;
      var sample;
      var sample_fraction;

      this.ctx.clearRect(0, 0, this.width, this.height);

      for(i=0; i<this.width; i++)
        {
          sample_index = (i*dataLength)/ this.width;
          sample_index_f = sample_index | 0;
          sample = data[sample_index_f]
             + (sample_index - sample_index_f) * (data[sample_index_f+1] - data[sample_index_f]);
          sample_fraction = sample / 65535;
          sample = (sample * (256.0 / 65536)) | 0;
          this.ctx.fillStyle = "rgba("+this.map[sample][0]+","+this.map[sample][1]+","+this.map[sample][2]+",255)";
          this.ctx.fillRect(i, this.height-(sample_fraction * this.height), 2, 2);
        }
    };
}

function Waterfall(canvasFrontId, colourMap)
{
    this.width = document.getElementById(canvasFrontId).clientWidth;
    this.height = document.getElementById(canvasFrontId).clientHeight;
    this.map = colourMap;

    this.imgFront = document.getElementById(canvasFrontId);
    this.ctxFront = this.imgFront.getContext("2d");

    this.lineImage = this.ctxFront.createImageData(this.width, 1);

    this.addLine = function(data)
    {
      var dataLength = data.length;
      var imgdata = this.lineImage.data;
      var lookup = 0;
      var i = 0;
      let sample;
      let sample_fraction;
      let sample_index;
      let sample_index_f;

      for (lookup = 0; lookup < this.width; lookup++)
      {
        sample_index = (lookup*dataLength)/this.width;
        sample_index_f = sample_index | 0;
        sample = data[sample_index_f]
           + (sample_index - sample_index_f) * (data[sample_index_f+1] - data[sample_index_f]);
        sample_fraction = sample * (256 / 65536);
        var rgb = this.map[sample_fraction | 0];
        imgdata[i++] = rgb[0];
        imgdata[i++] = rgb[1];
        imgdata[i++] = rgb[2];
        imgdata[i++] = 255;
      }
      this.ctxFront.drawImage(this.imgFront, 
                    0, 0, this.width, this.height - 1, 
                    0, 1, this.width, this.height - 1);
      this.ctxFront.putImageData(this.lineImage, 0, 0);

      if (this.existingHeight < (this.height-1))
      {
        this.existingHeight++;
      }
    };
}

function ColourMap()
{
  var map = new Array(256);

  var e;
  for (e = 0; 64 > e; e++)
  {
    map[e] = new Uint8Array(3);
    map[e][0] = 0;
    map[e][1] = 0;
    map[e][2] = 64 + e;
  }
  for (; 128 > e; e++)
  {
    map[e] = new Uint8Array(3);
    map[e][0] = 3 * e - 192;
    map[e][1] = 0;
    map[e][2] = 64 + e;
  }
  for (; 192 > e; e++)
  {
    map[e] = new Uint8Array(3);
    map[e][0] = e + 64;
    map[e][1] = 256 * Math.sqrt((e - 128) / 64);
    map[e][2] = 511 - 2 * e;
  }
  for (; 256 > e; e++)
  {
    map[e] = new Uint8Array(3);
    map[e][0] = 255;
    map[e][1] = 255;
    map[e][2] = 512 + 2 * e;
  }

  return map;
}