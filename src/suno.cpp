#include "examples/common-sdl.h"
#include "examples/common.h"
#include "libs/whisper.h"
#include "examples/grammar-parser.h"

#include <sstream>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>
#include <map>




// handles the command line parameters
struct whisper_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t prompt_ms  = 5000;
    int32_t command_ms = 8000;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx  = 0;

	float vad_thold = 0.6f;
	float freq_thold = 100.0f;

	float grammar_penalty = 100.0f;

	grammar_parser::parse_state grammar_parsed;

    bool speed_up      = false;
    bool translate     = false;
    bool print_special = false;
    bool print_energy  = false;
    bool no_timestamps = true;
    bool use_gpu       = true;

	std::string language  = "en";
    std::string model     = "models/ggml-base.en.bin";
    std::string fname_out;
    std::string commands;
    std::string prompt;
    std::string context;
    std::string grammar;
};




std::string transcribe(
		whisper_context * ctx,
		const whisper_params & params,
	const std::vector<float> & pcmf32,
		   const std::string & grammar_rule,
					   float & logprob_min,
					   float & logprob_sum,
		                 int & n_tokens,
					 int64_t & t_ms){


	const auto t_start = std::chrono::high_resolution_clock::now();

	logprob_min = 0.0f;
	logprob_sum  =0.0f;
	n_tokens = 0;
	/* t_ms; */

	// TODO: not sure what they?
	// check the difference between both of those
	//whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);

	wparams.print_progress   = true;
    wparams.print_special    = params.print_special;
    wparams.print_realtime   = false;
    wparams.print_timestamps = !params.no_timestamps;
    wparams.translate        = params.translate;
    wparams.no_context       = true;
    wparams.no_timestamps    = params.no_timestamps;
    wparams.single_segment   = true;
    wparams.max_tokens       = params.max_tokens;
    wparams.language         = params.language.c_str();
    wparams.n_threads        = params.n_threads;


	wparams.audio_ctx = params.audio_ctx;
	wparams.speed_up = params.speed_up;

	// TODO: play around with the values for optimal perf
    wparams.temperature     = 0.4f;
    wparams.temperature_inc = 1.0f;
    wparams.greedy.best_of  = 5;

    wparams.beam_search.beam_size = 5;

    wparams.initial_prompt = params.context.data();


	const auto & grammar_parsed = params.grammar_parsed;
	auto grammar_rules  = grammar_parsed.c_rules();

    if (!params.grammar_parsed.rules.empty() && !grammar_rule.empty()) {
        if (grammar_parsed.symbol_ids.find(grammar_rule) == grammar_parsed.symbol_ids.end()) {
            fprintf(stderr, "%s: warning: grammar rule '%s' not found - skipping grammar sampling\n", __func__, grammar_rule.c_str());
        } else {
            wparams.grammar_rules   = grammar_rules.data();
            wparams.n_grammar_rules = grammar_rule.size();
            wparams.i_start_rule    = grammar_parsed.symbol_ids.at(grammar_rule);
            wparams.grammar_penalty = params.grammar_penalty;
        }
    }

    if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        return "";
    }

	std::string result;

	const int n_segments = whisper_full_n_segments(ctx);

	for (int i = 0; i < n_segments; ++i) {
		const char * text = whisper_full_get_segment_text(ctx, i);

		result += text;

		const int n = whisper_full_n_tokens(ctx, i);
		for (int j =0; i < n; ++j){
			const auto token = whisper_full_get_token_data(ctx, i, j);

			if(token.plog > 0.0f) exit(0);
			logprob_min =  std::min(logprob_min, token.plog);
			logprob_sum += token.plog;
			++n_tokens;

		}
	}

	const auto t_end = std::chrono::high_resolution_clock::now();
	t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

	return result;

}



// converts a string to a vector of string with each word as a element
std::vector<std::string> get_words(const std::string &txt){
	std::vector<std::string> words;

	std::istringstream iss(txt);
	std::string word;
	while( iss >> word ){
		words.push_back(word);

	}
	return words;

}

bool file_exists(const std::string & fname) {
    std::ifstream f(fname.c_str());
    return f.good();
}


int prompt_transcription(struct whisper_context * ctx, audio_async & audio, const whisper_params & params) {
    bool is_running = true;
    bool ask_prompt = true;

    float logprob_min = 0.0f;
    float logprob_sum = 0.0f;
    int   n_tokens    = 0;

    std::vector<float> pcmf32_cur;

    const std::string k_prompt = params.prompt;

    const int k_prompt_length = get_words(k_prompt).size();

    fprintf(stderr, "\n");
    fprintf(stderr, "%s: always-prompt mode\n", __func__);

    // main loop
    while (is_running) {
        // handle Ctrl + C
        is_running = sdl_poll_events();

        // delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (ask_prompt) {
            fprintf(stdout, "\n");
            fprintf(stdout, "%s: The prompt is: '%s%s%s'\n", __func__, "\033[1m", k_prompt.c_str(), "\033[0m");
            fprintf(stdout, "\n");

            ask_prompt = false;
        }

        {
            audio.get(2000, pcmf32_cur);

            if (::vad_simple(pcmf32_cur, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, params.print_energy)) {
                fprintf(stdout, "%s: Speech detected! Processing ...\n", __func__);

                int64_t t_ms = 0;

                // detect the commands
                audio.get(params.command_ms, pcmf32_cur);

                const auto txt = ::trim(::transcribe(ctx, params, pcmf32_cur, "", logprob_min, logprob_sum, n_tokens, t_ms));

                const auto words = get_words(txt);

                std::string prompt;
                std::string command;

                for (int i = 0; i < (int) words.size(); ++i) {
                    if (i < k_prompt_length) {
                        prompt += words[i] + " ";
                    } else {
                        command += words[i] + " ";
                    }
                }

				// TODO: impli this from scratch
                const float sim = similarity(prompt, k_prompt);

                //debug
                //fprintf(stdout, "command size: %i\n", command_length);

                if ((sim > 0.7f) && (command.size() > 0)) {
                    fprintf(stdout, "%s: Command '%s%s%s', (t = %d ms)\n", __func__, "\033[1m", command.c_str(), "\033[0m", (int) t_ms);
                }

                fprintf(stdout, "\n");

                audio.clear();
            }
        }
    }

    return 0;

}

int process_general_transcription(struct whisper_context * ctx, audio_async & audio, const whisper_params & params) {
    bool is_running  = true;
    bool have_prompt = false;
    bool ask_prompt  = true;

    float logprob_min0 = 0.0f;
    float logprob_min  = 0.0f;

    float logprob_sum0 = 0.0f;
    float logprob_sum  = 0.0f;

    int n_tokens0 = 0;
    int n_tokens  = 0;

    std::vector<float> pcmf32_cur;
    std::vector<float> pcmf32_prompt;

    std::string k_prompt = "Ok Whisper, start listening for commands.";
    if (!params.prompt.empty()) {
        k_prompt = params.prompt;
    }

    fprintf(stderr, "\n");
    fprintf(stderr, "%s: general-purpose mode\n", __func__);

    // main loop
    while (is_running) {
        // handle Ctrl + C
        is_running = sdl_poll_events();

        // delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (ask_prompt) {
            fprintf(stdout, "\n");
            fprintf(stdout, "%s: Say the following phrase: '%s%s%s'\n", __func__, "\033[1m", k_prompt.c_str(), "\033[0m");
            fprintf(stdout, "\n");

            ask_prompt = false;
        }

        {
            audio.get(2000, pcmf32_cur);

            if (::vad_simple(pcmf32_cur, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, params.print_energy)) {
                fprintf(stdout, "%s: Speech detected! Processing ...\n", __func__);

                int64_t t_ms = 0;

                if (!have_prompt) {
                    // wait for activation phrase
                    audio.get(params.prompt_ms, pcmf32_cur);

                    const auto txt = ::trim(::transcribe(ctx, params, pcmf32_cur, "prompt", logprob_min0, logprob_sum0, n_tokens0, t_ms));

                    const float p = 100.0f * std::exp(logprob_min0);

                    fprintf(stdout, "%s: Heard '%s%s%s', (t = %d ms, p = %.2f%%)\n", __func__, "\033[1m", txt.c_str(), "\033[0m", (int) t_ms, p);

                    const float sim = similarity(txt, k_prompt);

                    if (txt.length() < 0.8*k_prompt.length() || txt.length() > 1.2*k_prompt.length() || sim < 0.8f) {
                        fprintf(stdout, "%s: WARNING: prompt not recognized, try again\n", __func__);
                        ask_prompt = true;
                    } else {
                        fprintf(stdout, "\n");
                        fprintf(stdout, "%s: The prompt has been recognized!\n", __func__);
                        fprintf(stdout, "%s: Waiting for voice commands ...\n", __func__);
                        fprintf(stdout, "\n");

                        // save the audio for the prompt
                        pcmf32_prompt = pcmf32_cur;
                        have_prompt = true;
                    }
                } else {
                    // we have heard the activation phrase, now detect the commands
                    audio.get(params.command_ms, pcmf32_cur);

                    //printf("len prompt:  %.4f\n", pcmf32_prompt.size() / (float) WHISPER_SAMPLE_RATE);
                    //printf("len command: %.4f\n", pcmf32_cur.size() / (float) WHISPER_SAMPLE_RATE);

                    // prepend 3 second of silence
                    pcmf32_cur.insert(pcmf32_cur.begin(), 3.0f*WHISPER_SAMPLE_RATE, 0.0f);

                    // prepend the prompt audio
                    pcmf32_cur.insert(pcmf32_cur.begin(), pcmf32_prompt.begin(), pcmf32_prompt.end());

                    const auto txt = ::trim(::transcribe(ctx, params, pcmf32_cur, "root", logprob_min, logprob_sum, n_tokens, t_ms));

                    //const float p = 100.0f * std::exp((logprob - logprob0) / (n_tokens - n_tokens0));
                    const float p = 100.0f * std::exp(logprob_min);

                    //fprintf(stdout, "%s: heard '%s'\n", __func__, txt.c_str());

                    // find the prompt in the text
                    float best_sim = 0.0f;
                    size_t best_len = 0;
                    for (size_t n = 0.8*k_prompt.size(); n <= 1.2*k_prompt.size(); ++n) {
                        if (n >= txt.size()) {
                            break;
                        }

                        const auto prompt = txt.substr(0, n);

                        const float sim = similarity(prompt, k_prompt);

                        //fprintf(stderr, "%s: prompt = '%s', sim = %f\n", __func__, prompt.c_str(), sim);

                        if (sim > best_sim) {
                            best_sim = sim;
                            best_len = n;
                        }
                    }

                    fprintf(stdout, "%s:   DEBUG: txt = '%s', prob = %.2f%%\n", __func__, txt.c_str(), p);
                    if (best_len == 0) {
                        fprintf(stdout, "%s: WARNING: command not recognized, try again\n", __func__);
                    } else {
                        // cut the prompt from the decoded text
                        const std::string command = ::trim(txt.substr(best_len));

                        fprintf(stdout, "%s: Command '%s%s%s', (t = %d ms)\n", __func__, "\033[1m", command.c_str(), "\033[0m", (int) t_ms);
                    }

                    fprintf(stdout, "\n");
                }

                audio.clear();
            }
        }
    }

    return 0;

}


int main(int argc, char ** argv){
	whisper_params params;


	// init whisper
	struct whisper_context_params cparams;
	cparams.use_gpu = params.use_gpu;

	struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);

	// init audio
	audio_async audio(30*1000);

    if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE)) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        return 1;
    }



	audio.resume();

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	audio.clear();

	int ret_val = 0;
	// grammar magic i dont really understand but eh
    if (!params.grammar.empty()) {
        auto & grammar = params.grammar_parsed;
        if (file_exists(params.grammar.c_str())) {
            // read grammar from file
            std::ifstream ifs(params.grammar.c_str());
            const std::string txt = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            grammar = grammar_parser::parse(txt.c_str());
        } else {
            // read grammar from string
            grammar = grammar_parser::parse(params.grammar.c_str());
        }

        // will be empty (default) if there are parse errors
        if (grammar.rules.empty()) {
            ret_val = 1;
        } else {
            fprintf(stderr, "%s: grammar:\n", __func__);
            grammar_parser::print_grammar(stderr, grammar);
            fprintf(stderr, "\n");
        }
    }

	if(!params.prompt.empty() && params.grammar_parsed.rules.empty()){
		ret_val = prompt_transcription(ctx, audio, params);
	}else {
		ret_val = process_general_transcription(ctx, audio, params);
	}

	audio.pause();


	whisper_print_timings(ctx);
    whisper_free(ctx);

	return ret_val;
};




