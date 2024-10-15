基于esp32-adf 音频框架 和 火山引擎的语音技术 的语音聊天机器人
硬件平台：嘉立创实战派
软件：在esp32-adf框架上编程，与火山引擎采用wedsocket协议API对接。 
无需中转服务，esp32直接访问火山引擎服务器，
在实现过程中，因为esp32 adf 没有关于wedsocekt的流，所以我参考了HttP流，撰写了一个WedStream流，用于wedsocket协议的对接，本人技术有限，写的不好还请见谅
