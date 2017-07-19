var rpi_server = "http://relle.ufsc.br:8070";
var results;
var socket = '';
var switches = 0;
var UIimg_interval = null;
var circuit_images = [10,12,13,14,17,18,20,21,24,30,32,34,36,37,38,4,40,42,48,5,54,58,64,65,66,67,68,69,7,70,72,73,74,8,80,81,9,96,97];
$(function () {   
    
    $('.switch').bootstrapToggle({
        onstyle: "success",
        offstyle: "danger",
        size: "small"
    });
    
    function reset(){
        var message = {};
        message.sw = {};
        message.pass = $('meta[name=csrf-token]').attr('content');
      
        for (var i = 0; i < 7; i++) {
                message.sw[i] = 0;
        }
        if (message && socket) {
            message.pass = $('meta[name=csrf-token]').attr('content');
            socket.emit('new message', message);
        }
    }
    
    function sendMessage() {
        clearTimeout(UIimg_interval);
        switches = 0;
        var message = {};
        message.sw = {};
        message.pass = $('meta[name=csrf-token]').attr('content');
        for (var i = 0; i < 7; i++) {
            if ($("input[id='sw" + i + "']:checked").length) {
                message.sw[i] = 1;
                switches = switches | 1 << i;
                console.log('switches: ' + switches);
            } else {
                message.sw[i] = 0;
            }
        }
        if (message && socket) {
            message.pass = $('meta[name=csrf-token]').attr('content');
            socket.emit('new message', message);
        }
        UIimg_interval = setTimeout(function(){
            if(circuit_images.indexOf(switches) >= 0){
                console.log('loading /exp_data/3/resultante/' + switches+'.png');
                 $('.equivalent').attr('src', '/exp_data/3/equivalent/' + switches+'.png');
                $('.default_circuit').hide();
                $('.equivalent').show();
            }else{
                $('.equivalent').hide();
                $('.default_circuit').show();
                console.log('resultante '+ switches+' nao criada ainda');
            }
        },3000);
    }
    
    $('.switch').change(function () {
        sendMessage();
    });


    $.getScript(rpi_server + '/socket.io/socket.io.js', function () {
        // Initialize varibles
        // Prompt for setting a username
        socket = io.connect(rpi_server);
        socket.emit('new connection', {pass: $("#pass").html()});
        
        
        socket.on('new message', function (data) {
            console.log(data);
        });
        

        // Whenever the server emits 'user joined', log it in the chat body
        socket.on('data received', function (data) {
            //printLog(data);
            results = $.parseJSON(data);
            console.log("I'm receiving " + data);
            
            for (var i = 0; i < 7; i++) {
                if(!isNaN(results.amperemeter[i])){
                    $("#a" + i).html(parseFloat(results.amperemeter[i] / 1000).toFixed(1) + " mA");
                }
            }
            for (var i = 0; i < 2; i++) {
                if(!isNaN(results.amperemeter[i])){
                    $("#v" + i).html(parseFloat(results.voltmeter[i] / 1000).toFixed(2) + " V");
                }
            }

        });
        
    });
});

function report(id) {
    var array = results; //$.parseJSON(results);   //JSON formatado como variável results no topo
    //$.redirect('http://relle.ufsc.br/labs/'+ id + '/report', array);
    $.ajax({
        type: "POST",
        url: "http://relle.ufsc.br/labs/" + id + "/report/",
        data: array, // results, //{a2: 'i'}, // results,
        success: function (pdf) {
            var win = window.open("http://relle.ufsc.br/labs/" + id + "/report", '_blank');
         //   win.focus();
            console.log("Report created.");
        }
    });
}
