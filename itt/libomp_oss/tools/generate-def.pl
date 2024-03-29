#!/usr/bin/env perl

# <copyright>
#    Copyright (c) 2013 Intel Corporation.  All Rights Reserved.
#
#    Redistribution and use in source and binary forms, with or without
#    modification, are permitted provided that the following conditions
#    are met:
#
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#      * Neither the name of Intel Corporation nor the names of its
#        contributors may be used to endorse or promote products derived
#        from this software without specific prior written permission.
#
#    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#
#------------------------------------------------------------------------
#
#    Portions of this software are protected under the following patents:
#        U.S. Patent 5,812,852
#        U.S. Patent 6,792,599
#        U.S. Patent 7,069,556
#        U.S. Patent 7,328,433
#        U.S. Patent 7,500,242
#
# </copyright>

# Some pragmas.
use strict;          # Restrict unsafe constructs.
use warnings;        # Enable all warnings.

use FindBin;
use lib "$FindBin::Bin/lib";

use tools;

our $VERSION = "0.004";

#
# Subroutines.
#

sub parse_input($\%) {

    my ( $input, $defs ) = @_;
    my @bulk = read_file( $input );
    my %entries;
    my %ordinals;
    my @dirs;
    my $value = 1;

    my $error =
        sub {
            my ( $msg, $l, $line ) = @_;
            runtime_error(
                "Error parsing file \"$input\" line $l:\n" .
                "    $line" .
                ( $msg ? $msg . "\n" : () )
            );
        }; # sub

    my $n = 0;    # Line number.
    foreach my $line ( @bulk ) {
        ++ $n;
        if ( 0 ) {
        } elsif ( $line =~ m{^\s*(?:#|\n)} ) {
            # Empty line or comment. Skip it.
        } elsif ( $line =~ m{^\s*%} ) {
            # A directive.
            if ( 0  ) {
            } elsif ( $line =~ m{^\s*%\s*if(n)?def\s+([A-Za-z0-9_]+)\s*(?:#|\n)} ) {
                my ( $negation, $name ) = ( $1, $2 );
                my $dir = { n => $n, line => $line, name => $name, value => $value };
                push( @dirs, $dir );
                $value = ( $value and ( $negation xor $defs->{ $name } ) );
            } elsif ( $line =~ m{^\s*%\s*endif\s*(?:#|\n)} ) {
                if ( not @dirs ) {
                    $error->( "Orphan %endif directive.", $n, $line );
                }; # if
                my $dir = pop( @dirs );
                $value = $dir->{ value };
            } else {
                $error->( "Bad directive.", $n, $line );
            }; # if
        } elsif ( $line =~ m{^\s*(-)?\s*([A-Za-z0-9_]+)(?:\s+(\d+|DATA))?\s*(?:#|\n)} ) {
            my ( $obsolete, $entry, $ordinal ) = ( $1, $2, $3 );
            if ( $value ) {
                if ( exists( $entries{ $entry } ) ) {
                    $error->( "Entry \"$entry\" has already been specified.", $n, $line );
                }; # if
                $entries{ $entry } = { ordinal => $ordinal, obsolete => defined( $obsolete ) };
                if ( defined( $ordinal ) and $ordinal ne "DATA" ) {
                    if ( $ordinal >= 1000 and $entry =~ m{\A[ok]mp_} ) {
                        $error->( "Ordinal of user-callable entry must be < 1000", $n, $line );
                    }; # if
                    if ( $ordinal >= 1000 and $ordinal < 2000 ) {
                        $error->( "Ordinals between 1000 and 1999 are reserved.", $n, $line );
                    }; # if
                    if ( exists( $ordinals{ $ordinal } ) ) {
                        $error->( "Ordinal $ordinal has already been used.", $n, $line );
                    }; # if
                    $ordinals{ $ordinal } = $entry;
                }; # if
            }; # if
        } else {
            $error->( "", $n, $line );
        }; # if
    }; # foreach

    if ( @dirs ) {
        my $dir = pop( @dirs );
        $error->( "Unterminated %if direcive.", $dir->{ n }, $dir->{ line } );
    }; # while

    return %entries;

}; # sub parse_input

sub process(\%) {

    my ( $entries ) = @_;

    foreach my $entry ( keys( %$entries ) ) {
        if ( not $entries->{ $entry }->{ obsolete } ) {
            my $ordinal = $entries->{ $entry }->{ ordinal };
            if ( $entry =~ m{\A[ok]mp_} ) {
                if ( not defined( $ordinal ) or $ordinal eq "DATA" ) {
                    runtime_error(
                        "Bad entry \"$entry\": ordinal number is not specified."
                    );
                }; # if
                $entries->{ uc( $entry ) } = { ordinal => 1000 + $ordinal };
            }; # if
        }; # if
    }; # foreach

    return %$entries;

}; # sub process

sub generate_output(\%$) {

    my ( $entries, $output ) = @_;
    my $bulk;

    $bulk = "EXPORTS\n";
    foreach my $entry ( sort( keys( %$entries ) ) ) {
        if ( not $entries->{ $entry }->{ obsolete } ) {
            $bulk .= sprintf( "    %-40s ", $entry );
            my $ordinal = $entries->{ $entry }->{ ordinal };
            if ( defined( $ordinal ) ) {
                if ( $ordinal eq "DATA" ) {
                    $bulk .= "DATA";
                } else {
                    $bulk .= "\@" . $ordinal;
                }; # if
            }; # if
            $bulk .= "\n";
        }; # if
    }; # foreach
    if ( defined( $output ) ) {
        write_file( $output, \$bulk );
    } else {
        print( $bulk );
    }; # if

}; # sub generate_ouput

#
# Parse command line.
#

my $input;   # The name of input file.
my $output;  # The name of output file.
my %defs;

get_options(
    "output=s"    => \$output,
    "D|define=s"  =>
        sub {
            my ( $opt_name, $opt_value ) = @_;
            my ( $def_name, $def_value );
            if ( $opt_value =~ m{\A(.*?)=(.*)\z} ) {
                ( $def_name, $def_value ) = ( $1, $2 );
            } else {
                ( $def_name, $def_value ) = ( $opt_value, 1 );
            }; # if
            $defs{ $def_name } = $def_value;
        },
);

if ( @ARGV == 0 ) {
    cmdline_error( "Not enough arguments." );
}; # if
if ( @ARGV > 1 ) {
    cmdline_error( "Too many arguments." );
}; # if
$input = shift( @ARGV );

#
# Work.
#

my %data = parse_input( $input, %defs );
%data = process( %data );
generate_output( %data, $output );
exit( 0 );

__END__

#
# Embedded documentation.
#

=pod

=head1 NAME

B<generate-def.pl> -- Generate def file for OpenMP RTL.

=head1 SYNOPSIS

B<generate-def.pl> I<OPTION>... I<file>

=head1 OPTIONS

=over

=item B<--define=>I<name>[=I<value>]

=item B<-D> I<name>[=I<value>]

Define specified name. If I<value> is ommitted, I<name> is defined to 1. If I<value> is 0 or empty,
name is B<not> defined.

=item B<--output=>I<file>

=item B<-o> I<file>

Specify output file name. If option is not present, result is printed to stdout.

=item B<--doc>

=item B<--manual>

Print full help message and exit.

=item B<--help>

Print short help message and exit.

=item B<--usage>

Print very short usage message and exit.

=item B<--verbose>

Do print informational messages.

=item B<--version>

Print version and exit.

=item B<--quiet>

Work quiet, do not print informational messages.

=back

=head1 ARGUMENTS

=over

=item I<file>

A name of input file.

=back

=head1 DESCRIPTION

The script reads input file, process conditional directives, checks content for consistency, and
generates ouptput file suitable for linker.

=head2 Input File Format

=over

=item Comments

    # It's a comment.

Comments start with C<#> symbol and continue to the end of line.

=item Conditional Directives

    %ifdef name
    %ifndef name
    %endif

A part of file surronded by C<%ifdef I<name>> and C<%endif> directives is a conditional part -- it
has effect only if I<name> is defined in the comman line by B<--define> option. C<%ifndef> is a
negated version of C<%ifdef> -- conditional part has an effect only if I<name> is B<not> defined.

Conditional parts may be nested.

=item Export Definitions

    symbol
    symbol ordinal
    symbol DATA

Symbols starting with C<omp_> or C<kmp_> must have ordinal specified. They are subjects for special
processing: each symbol generates two output lines: original one and upper case version. The ordinal
number of the second is original ordinal increased by 1000.

=item Obsolete Symbols

    - symbol
    - symbol ordinal
    - symbol DATA

Obsolete symbols look like export definitions prefixed with minus sign. Obsolete symbols do not
affect the output, but obsolete symbols and their ordinals cannot be (re)used in export definitions.

=back

=head1 EXAMPLES

    $ generate-def.pl -D stub -D USE_TCHECK=0 -o libguide.def dllexport

=cut

# end of file #

