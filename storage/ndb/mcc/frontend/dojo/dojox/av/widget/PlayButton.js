//>>built
define("dojox/av/widget/PlayButton",["dojo","dijit","dijit/_Widget","dijit/_TemplatedMixin"],function(_1,_2){
_1.declare("dojox.av.widget.PlayButton",[_2._Widget,_2._TemplatedMixin],{templateString:_1.cache("dojox.av.widget","resources/PlayButton.html"),postCreate:function(){
this.showPlay();
},setMedia:function(_3){
this.media=_3;
_1.connect(this.media,"onEnd",this,"showPlay");
_1.connect(this.media,"onStart",this,"showPause");
},onClick:function(){
if(this._mode=="play"){
this.onPlay();
}else{
this.onPause();
}
},onPlay:function(){
if(this.media){
this.media.play();
}
this.showPause();
},onPause:function(){
if(this.media){
this.media.pause();
}
this.showPlay();
},showPlay:function(){
this._mode="play";
_1.removeClass(this.domNode,"Pause");
_1.addClass(this.domNode,"Play");
},showPause:function(){
this._mode="pause";
_1.addClass(this.domNode,"Pause");
_1.removeClass(this.domNode,"Play");
}});
return dojox.av.widget.PlayButton;
});
